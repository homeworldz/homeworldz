// Package api implements the browser-facing HomeWorldz website API described
// by homeworldz.com/api/openapi.yaml: email-verified avatar registration,
// stateless website authentication, self-service account management, and
// privileged user/ban/region administration.
//
// It is intentionally separate from the grid's service-token internal API and
// from viewer login: it runs as its own binary on its own port, applies a
// browser-oriented middleware chain (CORS, rate limiting), and authenticates
// with short-lived website JWTs that carry no in-world meaning.
package api

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"strings"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/mailer"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
	"github.com/homeworldz/homeworldz/grid/internal/webtoken"
)

// AccountStore is the account persistence the API depends on. It is satisfied
// by *webaccount.PostgresStore.
type AccountStore interface {
	Register(ctx context.Context, displayName, email string) (webaccount.Account, string, error)
	Verify(ctx context.Context, code, password string) (webaccount.Account, error)
	ResendVerification(ctx context.Context, userid string) (code, email string, err error)
	Authenticate(ctx context.Context, userid, password string) (webaccount.Account, error)
	Get(ctx context.Context, id string) (webaccount.Account, error)
	GetManaged(ctx context.Context, id string) (webaccount.ManagedAccount, error)
	UpdateProfile(ctx context.Context, id, displayName string) (webaccount.Account, error)
	ChangePassword(ctx context.Context, id, currentPassword, newPassword string) error
	List(ctx context.Context, search, cursor string, limit int) ([]webaccount.ManagedAccount, string, error)
	ReplacePrivileges(ctx context.Context, id, privs string) (webaccount.ManagedAccount, error)
	Ban(ctx context.Context, id, reason string, expiresAt *time.Time, bannedBy string) (webaccount.ManagedAccount, error)
	Unban(ctx context.Context, id string) (webaccount.ManagedAccount, error)
}

// RegionStore is the provisioned-region persistence the API depends on. It is
// satisfied by *provisioning.PostgresStore.
type RegionStore interface {
	List(ctx context.Context) ([]provisioning.Region, error)
	Get(ctx context.Context, id string) (provisioning.Region, error)
	Create(ctx context.Context, region provisioning.Region) (provisioning.Region, error)
	Update(ctx context.Context, id string, update provisioning.Update) (provisioning.Region, error)
	RotateAccessKey(ctx context.Context, id, accessKey string) (provisioning.Region, error)
	Delete(ctx context.Context, id string) error
}

// LeaseStore exposes just enough of the live-region lease store to derive
// online state and end a lease on undeploy. It is satisfied by
// *regions.PostgresStore.
type LeaseStore interface {
	Get(ctx context.Context, id string) (regions.Region, error)
	DeregisterProvisioned(ctx context.Context, id string) error
}

// Options configures New.
type Options struct {
	Accounts        AccountStore
	Regions         RegionStore
	Leases          LeaseStore
	Signer          *webtoken.Signer
	Mailer          mailer.Mailer
	Logger          *slog.Logger
	AllowedOrigins  []string
	VerificationURL string
	RatePerMinute   int
	RateBurst       int
}

// API is the website API handler.
type API struct {
	accounts        AccountStore
	regions         RegionStore
	leases          LeaseStore
	signer          *webtoken.Signer
	mailer          mailer.Mailer
	logger          *slog.Logger
	allowedOrigins  map[string]bool
	verificationURL string
	limiter         *rateLimiter
}

// New validates options and returns the composed website API handler.
func New(options Options) (http.Handler, error) {
	if options.Signer == nil {
		return nil, errors.New("api: signer is required")
	}
	if options.Mailer == nil {
		return nil, errors.New("api: mailer is required")
	}
	origins := map[string]bool{}
	for _, origin := range options.AllowedOrigins {
		trimmed := strings.TrimRight(strings.TrimSpace(origin), "/")
		if trimmed != "" {
			origins[trimmed] = true
		}
	}
	perMinute := options.RatePerMinute
	if perMinute <= 0 {
		perMinute = 30
	}
	burst := options.RateBurst
	if burst <= 0 {
		burst = 10
	}
	a := &API{
		accounts:        options.Accounts,
		regions:         options.Regions,
		leases:          options.Leases,
		signer:          options.Signer,
		mailer:          options.Mailer,
		logger:          options.Logger,
		allowedOrigins:  origins,
		verificationURL: strings.TrimRight(options.VerificationURL, "/"),
		limiter:         newRateLimiter(float64(perMinute)/60.0, burst),
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/v1/registrations", a.registrations)
	mux.HandleFunc("/v1/verifications", a.verifications)
	mux.HandleFunc("/v1/verifications/resend", a.resendVerification)
	mux.HandleFunc("/v1/tokens", a.tokens)
	mux.HandleFunc("/v1/account", a.account)
	mux.HandleFunc("/v1/account/profile", a.accountProfile)
	mux.HandleFunc("/v1/account/password", a.accountPassword)
	mux.HandleFunc("/v1/admin/users", a.adminUsersRoot)
	mux.HandleFunc("/v1/admin/users/", a.adminUserByID)
	mux.HandleFunc("/v1/admin/regions", a.adminRegionsRoot)
	mux.HandleFunc("/v1/admin/regions/", a.adminRegionByID)
	mux.HandleFunc("/", a.notFound)

	return withRecovery(withRequestID(withRequestLogging(
		a.withCORS(mux), a.logger)), a.logger), nil
}

func (a *API) notFound(w http.ResponseWriter, _ *http.Request) {
	writeError(w, http.StatusNotFound, Error{Code: "not_found", Message: "route not found"})
}

// methodNotAllowed writes a 405 with an Allow header listing supported methods.
func methodNotAllowed(w http.ResponseWriter, allow ...string) {
	w.Header().Set("Allow", strings.Join(allow, ", "))
	writeError(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "method not allowed"})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

// writeError writes an error body without caching.
func writeError(w http.ResponseWriter, status int, body Error) {
	writeJSON(w, status, body)
}

// decodeJSON reads exactly one JSON object into target, rejecting unknown
// fields and trailing data, matching the contract's additionalProperties:false.
func decodeJSON(w http.ResponseWriter, r *http.Request, target any) bool {
	decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 64*1024))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(target); err != nil {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_json", Message: "request body must be valid JSON"})
		return false
	}
	if err := decoder.Decode(&struct{}{}); !errors.Is(err, io.EOF) {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_json", Message: "request body must contain one JSON object"})
		return false
	}
	return true
}

func validUUID(value string) bool {
	if len(value) != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-' {
		return false
	}
	for index, character := range value {
		if index == 8 || index == 13 || index == 18 || index == 23 {
			continue
		}
		if !((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F')) {
			return false
		}
	}
	return true
}

// identityOf maps a stored account to the public Identity DTO.
func identityOf(account webaccount.Account) Identity {
	return Identity{
		ID:          account.ID,
		Userid:      account.Userid,
		DisplayName: account.DisplayName,
		RezDate:     account.RezDate.UTC(),
		Privs:       account.Privileges,
	}
}

// managedUserOf maps a managed account to the ManagedUser DTO.
func managedUserOf(account webaccount.ManagedAccount) ManagedUser {
	user := ManagedUser{Identity: identityOf(account.Account), State: account.State}
	if account.Ban != nil {
		ban := &Ban{Reason: account.Ban.Reason, BannedAt: account.Ban.BannedAt.UTC(), BannedBy: account.Ban.BannedBy}
		if account.Ban.ExpiresAt != nil {
			expires := account.Ban.ExpiresAt.UTC()
			ban.ExpiresAt = &expires
		}
		user.Ban = ban
	}
	return user
}
