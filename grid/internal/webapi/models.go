package webapi

import "time"

// Error is the uniform error body. Code is a stable machine-readable slug.
type Error struct {
	Code    string `json:"code"`
	Message string `json:"message"`
	Field   string `json:"field,omitempty"`
}

// Identity is the public avatar identity.
type Identity struct {
	ID          string    `json:"id"`
	Userid      string    `json:"userid"`
	DisplayName string    `json:"displayName"`
	RezDate     time.Time `json:"rezDate"`
	Privs       string    `json:"privs"`
}

// Ban is the account-suspension detail in a ManagedUser.
type Ban struct {
	Reason    string     `json:"reason"`
	ExpiresAt *time.Time `json:"expiresAt,omitempty"`
	BannedAt  time.Time  `json:"bannedAt"`
	BannedBy  string     `json:"bannedBy"`
}

// ManagedUser is an Identity plus administrative state.
type ManagedUser struct {
	Identity
	State string `json:"state"`
	Ban   *Ban   `json:"ban,omitempty"`
}

// UserPage is a page of managed users.
type UserPage struct {
	Users      []ManagedUser `json:"users"`
	NextCursor string        `json:"nextCursor,omitempty"`
}

// TokenResponse carries an issued website token and the authenticated identity.
type TokenResponse struct {
	AccessToken string    `json:"accessToken"`
	TokenType   string    `json:"tokenType"`
	ExpiresAt   time.Time `json:"expiresAt"`
	Identity    Identity  `json:"identity"`
}

// RegistrationPending is returned by registration; it echoes the derived userid.
type RegistrationPending struct {
	Userid      string `json:"userid"`
	DisplayName string `json:"displayName"`
}

// ManagedRegion is a provisioned region with derived online state.
type ManagedRegion struct {
	ID             string     `json:"id"`
	Name           string     `json:"name"`
	OwnerUserID    string     `json:"ownerUserId"`
	GridX          *int       `json:"gridX"`
	GridY          *int       `json:"gridY"`
	PublicEndpoint string     `json:"publicEndpoint"`
	ViewerPort     int        `json:"viewerPort"`
	Enabled        bool       `json:"enabled"`
	State          string     `json:"state"`
	LeaseExpiresAt *time.Time `json:"leaseExpiresAt,omitempty"`
}

// RegionList is a list of provisioned regions.
type RegionList struct {
	Regions []ManagedRegion `json:"regions"`
}

// RegionDeployment returns a region together with a one-time access key.
type RegionDeployment struct {
	Region    ManagedRegion `json:"region"`
	AccessKey string        `json:"accessKey"`
}

// Request bodies.

type registerAvatarRequest struct {
	DisplayName string `json:"displayName"`
	Email       string `json:"email"`
}

type verifyRegistrationRequest struct {
	Code     string `json:"code"`
	Password string `json:"password"`
}

type resendVerificationRequest struct {
	Userid string `json:"userid"`
}

type createTokenRequest struct {
	Userid   string `json:"userid"`
	Password string `json:"password"`
}

type updateProfileRequest struct {
	DisplayName *string `json:"displayName"`
}

type changePasswordRequest struct {
	CurrentPassword string `json:"currentPassword"`
	NewPassword     string `json:"newPassword"`
}

type replacePrivilegesRequest struct {
	Privs string `json:"privs"`
}

type banUserRequest struct {
	Reason    string     `json:"reason"`
	ExpiresAt *time.Time `json:"expiresAt"`
}

type createRegionRequest struct {
	Name           string `json:"name"`
	OwnerUserID    string `json:"ownerUserId"`
	GridX          *int   `json:"gridX"`
	GridY          *int   `json:"gridY"`
	PublicEndpoint string `json:"publicEndpoint"`
	ViewerPort     *int   `json:"viewerPort"`
}

type updateRegionRequest struct {
	Name           *string `json:"name"`
	OwnerUserID    *string `json:"ownerUserId"`
	PublicEndpoint *string `json:"publicEndpoint"`
	ViewerPort     *int    `json:"viewerPort"`
}

type mapPositionRequest struct {
	GridX *int `json:"gridX"`
	GridY *int `json:"gridY"`
}
