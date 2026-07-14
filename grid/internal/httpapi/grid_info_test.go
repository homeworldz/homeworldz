package httpapi

import (
	"encoding/xml"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestViewerGridInfo(t *testing.T) {
	handler := New(nil, "test", Options{GridPublicURL: "http://grid.example:8002/"})
	request := httptest.NewRequest(http.MethodGet, "/get_grid_info", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusOK || response.Header().Get("Content-Type") != "application/xml; charset=utf-8" {
		t.Fatalf("status = %d, content type = %q", response.Code, response.Header().Get("Content-Type"))
	}
	var info viewerGridInfo
	if err := xml.Unmarshal(response.Body.Bytes(), &info); err != nil {
		t.Fatalf("decode grid info: %v", err)
	}
	if info.GridNick != "homeworldz" || info.GridName != "HomeWorldz" || info.Platform != "OpenSim" ||
		info.Login != "http://grid.example:8002/login" || info.Welcome != "http://grid.example:8002/welcome" ||
		info.Helper != "http://grid.example:8002/" {
		t.Fatalf("unexpected grid info: %#v", info)
	}
}

func TestViewerWelcomePage(t *testing.T) {
	request := httptest.NewRequest(http.MethodGet, "/welcome", nil)
	response := httptest.NewRecorder()
	New(nil, "test", Options{}).ServeHTTP(response, request)
	if response.Code != http.StatusOK || response.Header().Get("Content-Type") != "text/html; charset=utf-8" ||
		!strings.Contains(response.Body.String(), `src="data:image/svg+xml;base64,`) {
		t.Fatalf("unexpected welcome response: %d %q", response.Code, response.Body.String())
	}
}

func TestViewerLoginGETShowsWelcomePage(t *testing.T) {
	request := httptest.NewRequest(http.MethodGet, "/login", nil)
	response := httptest.NewRecorder()
	New(nil, "test", Options{}).ServeHTTP(response, request)
	if response.Code != http.StatusOK || response.Header().Get("Content-Type") != "text/html; charset=utf-8" ||
		!strings.Contains(response.Body.String(), `src="data:image/svg+xml;base64,`) {
		t.Fatalf("unexpected login page response: %d %q", response.Code, response.Body.String())
	}
}

func TestViewerLogo(t *testing.T) {
	request := httptest.NewRequest(http.MethodGet, "/assets/homeworldz.svg", nil)
	response := httptest.NewRecorder()
	New(nil, "test", Options{}).ServeHTTP(response, request)
	if response.Code != http.StatusOK || response.Header().Get("Content-Type") != "image/svg+xml; charset=utf-8" ||
		!strings.Contains(response.Body.String(), "<svg") {
		t.Fatalf("unexpected logo response: %d %q", response.Code, response.Body.String())
	}
}
