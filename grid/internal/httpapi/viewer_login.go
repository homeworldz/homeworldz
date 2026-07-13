package httpapi

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"encoding/binary"
	"encoding/hex"
	"encoding/xml"
	"errors"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

type rpcMethodCall struct {
	Method string    `xml:"methodName"`
	Params rpcParams `xml:"params"`
}
type rpcParams struct {
	Items []rpcParam `xml:"param"`
}
type rpcParam struct {
	Value rpcValue `xml:"value"`
}

type rpcValue struct {
	Text    string     `xml:",chardata"`
	String  *string    `xml:"string"`
	Integer *string    `xml:"int"`
	I4      *string    `xml:"i4"`
	Boolean *string    `xml:"boolean"`
	Struct  *rpcStruct `xml:"struct"`
	Array   *rpcArray  `xml:"array"`
}
type rpcStruct struct {
	Members []rpcMember `xml:"member"`
}
type rpcMember struct {
	Name  string   `xml:"name"`
	Value rpcValue `xml:"value"`
}
type rpcArray struct {
	Values []rpcValue `xml:"data>value"`
}

func (v rpcValue) text() string {
	if v.String != nil {
		return *v.String
	}
	if v.Integer != nil {
		return *v.Integer
	}
	if v.I4 != nil {
		return *v.I4
	}
	if v.Boolean != nil {
		return *v.Boolean
	}
	return strings.TrimSpace(v.Text)
}

func (v rpcValue) fields() map[string]rpcValue {
	result := make(map[string]rpcValue)
	if v.Struct != nil {
		for _, member := range v.Struct.Members {
			result[member.Name] = member.Value
		}
	}
	return result
}

type rpcOutputValue struct {
	String  *string          `xml:"string,omitempty"`
	Integer *int             `xml:"int,omitempty"`
	Boolean *int             `xml:"boolean,omitempty"`
	Struct  *rpcOutputStruct `xml:"struct,omitempty"`
	Array   *rpcOutputArray  `xml:"array,omitempty"`
}
type rpcOutputStruct struct {
	Members []rpcOutputMember `xml:"member"`
}
type rpcOutputMember struct {
	Name  string         `xml:"name"`
	Value rpcOutputValue `xml:"value"`
}
type rpcOutputArray struct {
	Values []rpcOutputValue `xml:"data>value"`
}
type rpcMethodResponse struct {
	XMLName xml.Name       `xml:"methodResponse"`
	Value   rpcOutputValue `xml:"params>param>value"`
}

func rpcString(value string) rpcOutputValue { return rpcOutputValue{String: &value} }
func rpcInt(value int) rpcOutputValue       { return rpcOutputValue{Integer: &value} }
func rpcBool(value bool) rpcOutputValue {
	number := 0
	if value {
		number = 1
	}
	return rpcOutputValue{Boolean: &number}
}
func rpcArrayValue(values ...rpcOutputValue) rpcOutputValue {
	return rpcOutputValue{Array: &rpcOutputArray{Values: values}}
}
func rpcStructValue(members ...rpcOutputMember) rpcOutputValue {
	return rpcOutputValue{Struct: &rpcOutputStruct{Members: members}}
}
func rpcField(name string, value rpcOutputValue) rpcOutputMember {
	return rpcOutputMember{Name: name, Value: value}
}

func inventoryFolderID(userID string, folderType int) string {
	value := sha256.Sum256([]byte(userID + "\x00homeworldz-inventory-folder\x00" + strconv.Itoa(folderType)))
	value[6] = (value[6] & 0x0f) | 0x80
	value[8] = (value[8] & 0x3f) | 0x80
	encoded := hex.EncodeToString(value[:16])
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" +
		encoded[16:20] + "-" + encoded[20:32]
}

func inventoryFolder(name, id, parent string, folderType int) rpcOutputValue {
	return rpcStructValue(
		rpcField("name", rpcString(name)),
		rpcField("folder_id", rpcString(id)),
		rpcField("parent_id", rpcString(parent)),
		rpcField("version", rpcInt(1)),
		rpcField("type_default", rpcInt(folderType)),
	)
}

func inventorySkeleton(userID string) (string, []rpcOutputValue) {
	rootID := inventoryFolderID(userID, 8)
	folders := []rpcOutputValue{inventoryFolder("My Inventory", rootID,
		"00000000-0000-0000-0000-000000000000", 8)}
	for _, folder := range []struct {
		name       string
		folderType int
	}{
		{"Textures", 0}, {"Sounds", 1}, {"Calling Cards", 2}, {"Landmarks", 3},
		{"Clothing", 5}, {"Objects", 6}, {"Notecards", 7}, {"Scripts", 10},
		{"Body Parts", 13}, {"Trash", 14}, {"Photo Album", 15}, {"Lost And Found", 16},
		{"Animations", 20}, {"Gestures", 21}, {"Favorites", 23}, {"Current Outfit", 46},
		{"My Outfits", 48}, {"Received Items", 50}, {"Settings", 56}, {"Materials", 57},
	} {
		folders = append(folders, inventoryFolder(folder.name,
			inventoryFolderID(userID, folder.folderType), rootID, folder.folderType))
	}
	return rootID, folders
}

func (a *API) viewerLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeViewerLogin(w, loginFailure("method", "Only POST is supported."))
		return
	}
	if a.identity == nil || a.regions == nil {
		writeViewerLogin(w, loginFailure("unavailable", "The HomeWorldz grid is not ready."))
		return
	}
	var call rpcMethodCall
	decoder := xml.NewDecoder(http.MaxBytesReader(w, r.Body, 1024*1024))
	if err := decoder.Decode(&call); err != nil || call.Method != "login_to_simulator" || len(call.Params.Items) != 1 {
		writeViewerLogin(w, loginFailure("key", "The viewer login request is invalid."))
		return
	}
	if err := decoder.Decode(&struct{}{}); !errors.Is(err, io.EOF) {
		writeViewerLogin(w, loginFailure("key", "The viewer login request contains trailing data."))
		return
	}
	fields := call.Params.Items[0].Value.fields()
	first, last := strings.TrimSpace(fields["first"].text()), strings.TrimSpace(fields["last"].text())
	username := strings.ToLower(first)
	if last != "" && !strings.EqualFold(last, "Resident") {
		username += "." + strings.ToLower(last)
	}
	passwordHash := strings.TrimPrefix(fields["passwd"].text(), "$1$")
	if first == "" || len(passwordHash) != 32 {
		writeViewerLogin(w, loginFailure("key", "The username or password is incorrect."))
		return
	}
	session, err := a.identity.CreateViewerSession(r.Context(), username, strings.ToLower(passwordHash), 12*time.Hour)
	if errors.Is(err, identity.ErrInvalidCredentials) {
		writeViewerLogin(w, loginFailure("key", "The username or password is incorrect."))
		return
	}
	if err != nil {
		writeViewerLogin(w, loginFailure("unavailable", "The HomeWorldz grid could not create a session."))
		return
	}
	region, err := resolveDestination(r.Context(), a.regions, fields["start"].text())
	if err != nil {
		_ = a.identity.RevokeSession(r.Context(), session.ID)
		writeViewerLogin(w, loginFailure("destination", "No online region matches the requested destination."))
		return
	}
	endpoint, err := url.Parse(region.PublicEndpoint)
	if err != nil || endpoint.Hostname() == "" {
		_ = a.identity.RevokeSession(r.Context(), session.ID)
		writeViewerLogin(w, loginFailure("unavailable", "The destination region endpoint is invalid."))
		return
	}
	circuit, err := newCircuitCode()
	if err != nil {
		_ = a.identity.RevokeSession(r.Context(), session.ID)
		writeViewerLogin(w, loginFailure("unavailable", "The grid could not allocate a viewer circuit."))
		return
	}
	if err := a.identity.AssignViewerDestination(r.Context(), session.ID, circuit, region.ID); err != nil {
		_ = a.identity.RevokeSession(r.Context(), session.ID)
		writeViewerLogin(w, loginFailure("unavailable", "The grid could not assign the viewer circuit."))
		return
	}
	rootID, skeleton := inventorySkeleton(session.UserID)
	root := rpcStructValue(rpcField("folder_id", rpcString(rootID)))
	libraryRoot := rpcStructValue(rpcField("folder_id", rpcString("00000000-0000-0000-0000-000000000001")))
	libraryOwner := rpcStructValue(rpcField("agent_id", rpcString("00000000-0000-0000-0000-000000000002")))
	response := rpcStructValue(
		rpcField("login", rpcString("true")), rpcField("message", rpcString("Welcome to HomeWorldz")),
		rpcField("agent_id", rpcString(session.UserID)), rpcField("session_id", rpcString(session.ID)),
		rpcField("secure_session_id", rpcString(session.SecureID)), rpcField("first_name", rpcString(first)),
		rpcField("last_name", rpcString(last)), rpcField("circuit_code", rpcInt(int(circuit))),
		rpcField("sim_ip", rpcString(endpoint.Hostname())), rpcField("sim_port", rpcInt(region.ViewerPort)),
		rpcField("region_x", rpcInt(region.GridX*256)), rpcField("region_y", rpcInt(region.GridY*256)),
		rpcField("region_size_x", rpcInt(256)), rpcField("region_size_y", rpcInt(256)),
		rpcField("start_location", rpcString(normalizeStart(fields["start"].text()))),
		rpcField("look_at", rpcString("[r1,r0,r0]")),
		rpcField("seed_capability", rpcString(strings.TrimRight(region.PublicEndpoint, "/")+"/caps/seed/"+session.ID)),
		rpcField("seconds_since_epoch", rpcInt(int(time.Now().Unix()))),
		rpcField("inventory-root", rpcArrayValue(root)), rpcField("inventory-skeleton", rpcArrayValue(skeleton...)),
		rpcField("inventory-lib-root", rpcArrayValue(libraryRoot)), rpcField("inventory-lib-owner", rpcArrayValue(libraryOwner)),
		rpcField("inventory-skel-lib", rpcArrayValue()), rpcField("login-flags", rpcArrayValue()),
		rpcField("gestures", rpcArrayValue()), rpcField("buddy-list", rpcArrayValue()),
	)
	writeViewerLogin(w, response)
}

func resolveDestination(ctx context.Context, store regions.Store, start string) (regions.Region, error) {
	items, err := store.List(ctx)
	if err != nil || len(items) == 0 {
		return regions.Region{}, regions.ErrNotFound
	}
	if strings.HasPrefix(strings.ToLower(start), "uri:") {
		name := strings.TrimPrefix(start, "uri:")
		if before, _, found := strings.Cut(name, "&"); found {
			name = before
		}
		for _, region := range items {
			if strings.EqualFold(strings.TrimSpace(name), region.Name) {
				return region, nil
			}
		}
		return regions.Region{}, regions.ErrNotFound
	}
	return items[0], nil
}

func normalizeStart(start string) string {
	if strings.HasPrefix(strings.ToLower(start), "uri:") {
		return "url"
	}
	if strings.EqualFold(start, "home") {
		return "home"
	}
	return "last"
}

func newCircuitCode() (uint32, error) {
	var data [4]byte
	if _, err := rand.Read(data[:]); err != nil {
		return 0, err
	}
	value := binary.BigEndian.Uint32(data[:])
	value &= 0x7fffffff
	if value == 0 {
		value = 1
	}
	return value, nil
}

func loginFailure(reason, message string) rpcOutputValue {
	return rpcStructValue(rpcField("login", rpcString("false")), rpcField("reason", rpcString(reason)), rpcField("message", rpcString(message)))
}

func writeViewerLogin(w http.ResponseWriter, value rpcOutputValue) {
	w.Header().Set("Content-Type", "text/xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte(xml.Header))
	_ = xml.NewEncoder(w).Encode(rpcMethodResponse{Value: value})
}
