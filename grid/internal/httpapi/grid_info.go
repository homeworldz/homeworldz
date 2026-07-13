package httpapi

import (
	"encoding/xml"
	"net/http"
)

type viewerGridInfo struct {
	XMLName  xml.Name `xml:"gridinfo"`
	GridNick string   `xml:"gridnick"`
	GridName string   `xml:"gridname"`
	Platform string   `xml:"platform"`
	Login    string   `xml:"login"`
	Welcome  string   `xml:"welcome"`
	Helper   string   `xml:"helperuri"`
}

func (a *API) gridInfo(w http.ResponseWriter, _ *http.Request) {
	contents, err := xml.Marshal(viewerGridInfo{
		GridNick: "homeworldz",
		GridName: "HomeWorldz",
		Platform: "OpenSim",
		Login:    a.publicURL + "/login",
		Welcome:  a.publicURL + "/welcome",
		Helper:   a.publicURL + "/",
	})
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "grid_info_error", Message: "grid information is unavailable"})
		return
	}
	w.Header().Set("Content-Type", "application/xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(append([]byte(xml.Header), contents...))
}

func (a *API) welcome(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte("<!doctype html><html><head><title>HomeWorldz</title></head>" +
		"<body><h1>HomeWorldz</h1><p>Welcome to the HomeWorldz development grid.</p></body></html>"))
}
