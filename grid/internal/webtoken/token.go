// Package webtoken issues and verifies the short-lived HS256 JWTs the website
// API uses to authenticate an avatar identity. The token carries the complete
// website identity plus an authorization-version claim; it is not a viewer
// session and never authorizes an in-world connection.
//
// It is implemented without an external JWT dependency to keep the grid module
// dependency set minimal, matching the rest of the codebase.
package webtoken

import (
	"crypto/hmac"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"time"
)

var (
	// ErrInvalidToken is returned for any structural, signature, or claim
	// failure. Callers must not distinguish the specific cause to clients.
	ErrInvalidToken = errors.New("invalid token")
	// ErrExpired is returned when a token is well-formed and correctly signed
	// but past its expiry.
	ErrExpired = errors.New("token expired")
)

// Claims is the website identity carried by a token. Times are encoded as the
// listed JSON representations: iat/exp as numeric Unix seconds, rezDate as
// RFC 3339. It never contains password material.
type Claims struct {
	Issuer      string
	Audience    string
	Subject     string
	Userid      string
	DisplayName string
	RezDate     time.Time
	Privs       string
	Version     int
	IssuedAt    time.Time
	ExpiresAt   time.Time
}

type payload struct {
	Issuer      string `json:"iss"`
	Audience    string `json:"aud"`
	Subject     string `json:"sub"`
	Userid      string `json:"userid"`
	DisplayName string `json:"displayName"`
	RezDate     string `json:"rezDate"`
	Privs       string `json:"privs"`
	Version     int    `json:"ver"`
	IssuedAt    int64  `json:"iat"`
	ExpiresAt   int64  `json:"exp"`
}

const header = `{"alg":"HS256","typ":"JWT"}`

// Signer signs and verifies tokens for a single issuer/audience with a fixed
// lifetime. It is safe for concurrent use.
type Signer struct {
	secret   []byte
	issuer   string
	audience string
	ttl      time.Duration
}

// NewSigner validates its inputs and returns a Signer. The secret must be at
// least 32 bytes so an HS256 key has adequate entropy.
func NewSigner(secret []byte, issuer, audience string, ttl time.Duration) (*Signer, error) {
	if len(secret) < 32 {
		return nil, errors.New("webtoken: secret must be at least 32 bytes")
	}
	if strings.TrimSpace(issuer) == "" || strings.TrimSpace(audience) == "" {
		return nil, errors.New("webtoken: issuer and audience are required")
	}
	if ttl <= 0 {
		return nil, errors.New("webtoken: ttl must be positive")
	}
	key := make([]byte, len(secret))
	copy(key, secret)
	return &Signer{secret: key, issuer: issuer, audience: audience, ttl: ttl}, nil
}

// TTL reports the token lifetime this signer applies.
func (s *Signer) TTL() time.Duration { return s.ttl }

// Sign issues a token for the given identity. The caller supplies subject,
// userid, display name, rez date, privileges, and authorization version; the
// issuer, audience, issued-at, and expiry are set by the signer. It returns the
// compact token and its absolute expiry.
func (s *Signer) Sign(now time.Time, subject, userid, displayName string, rezDate time.Time, privs string, version int) (string, time.Time, error) {
	issued := now.UTC().Truncate(time.Second)
	expires := issued.Add(s.ttl)
	body := payload{
		Issuer:      s.issuer,
		Audience:    s.audience,
		Subject:     subject,
		Userid:      userid,
		DisplayName: displayName,
		RezDate:     rezDate.UTC().Format(time.RFC3339),
		Privs:       privs,
		Version:     version,
		IssuedAt:    issued.Unix(),
		ExpiresAt:   expires.Unix(),
	}
	encodedPayload, err := json.Marshal(body)
	if err != nil {
		return "", time.Time{}, fmt.Errorf("webtoken: encode claims: %w", err)
	}
	signingInput := encodeSegment([]byte(header)) + "." + encodeSegment(encodedPayload)
	signature := s.mac(signingInput)
	return signingInput + "." + encodeSegment(signature), expires, nil
}

// Verify checks the signature and time claims and returns the decoded identity.
// It returns ErrExpired for an otherwise-valid but expired token and
// ErrInvalidToken for every other failure.
func (s *Signer) Verify(token string, now time.Time) (Claims, error) {
	firstDot := strings.IndexByte(token, '.')
	lastDot := strings.LastIndexByte(token, '.')
	if firstDot <= 0 || lastDot <= firstDot || strings.Count(token, ".") != 2 {
		return Claims{}, ErrInvalidToken
	}
	signingInput := token[:lastDot]
	headerSegment := token[:firstDot]
	payloadSegment := token[firstDot+1 : lastDot]
	signatureSegment := token[lastDot+1:]

	headerBytes, err := decodeSegment(headerSegment)
	if err != nil || string(headerBytes) != header {
		return Claims{}, ErrInvalidToken
	}
	signature, err := decodeSegment(signatureSegment)
	if err != nil {
		return Claims{}, ErrInvalidToken
	}
	expected := s.mac(signingInput)
	if subtle.ConstantTimeCompare(signature, expected) != 1 {
		return Claims{}, ErrInvalidToken
	}

	payloadBytes, err := decodeSegment(payloadSegment)
	if err != nil {
		return Claims{}, ErrInvalidToken
	}
	var body payload
	decoder := json.NewDecoder(strings.NewReader(string(payloadBytes)))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&body); err != nil {
		return Claims{}, ErrInvalidToken
	}
	if body.Issuer != s.issuer || body.Audience != s.audience || body.Subject == "" {
		return Claims{}, ErrInvalidToken
	}
	rezDate, err := time.Parse(time.RFC3339, body.RezDate)
	if err != nil {
		return Claims{}, ErrInvalidToken
	}
	reference := now.UTC()
	if body.ExpiresAt <= 0 || reference.After(time.Unix(body.ExpiresAt, 0)) {
		return Claims{}, ErrExpired
	}
	return Claims{
		Issuer:      body.Issuer,
		Audience:    body.Audience,
		Subject:     body.Subject,
		Userid:      body.Userid,
		DisplayName: body.DisplayName,
		RezDate:     rezDate,
		Privs:       body.Privs,
		Version:     body.Version,
		IssuedAt:    time.Unix(body.IssuedAt, 0).UTC(),
		ExpiresAt:   time.Unix(body.ExpiresAt, 0).UTC(),
	}, nil
}

func (s *Signer) mac(signingInput string) []byte {
	mac := hmac.New(sha256.New, s.secret)
	mac.Write([]byte(signingInput))
	return mac.Sum(nil)
}

func encodeSegment(value []byte) string {
	return base64.RawURLEncoding.EncodeToString(value)
}

func decodeSegment(value string) ([]byte, error) {
	return base64.RawURLEncoding.DecodeString(value)
}
