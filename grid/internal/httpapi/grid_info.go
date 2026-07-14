package httpapi

import (
	_ "embed"
	"encoding/base64"
	"encoding/xml"
	"net/http"
)

//go:embed assets/homeworldz.svg
var homeworldzLogo []byte

var homeworldzLogoDataURL = "data:image/svg+xml;base64," +
	base64.StdEncoding.EncodeToString(homeworldzLogo)

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
	w.Header().Set("Content-Security-Policy", "default-src 'none'; img-src data:; style-src 'unsafe-inline'")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write([]byte(`<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>HomeWorldz</title>
<style>
html,body{height:100%;margin:0}body{display:grid;place-items:center;background:#071923;color:#eaf7fb;font:16px system-ui,sans-serif;text-align:center}.panel{padding:2rem}.logo{display:block;width:min(80vw,28rem);height:auto;margin:0 auto 1.5rem}p{color:#b8d6df}
</style>
</head>
<body><main class="panel"><img class="logo" src="` + homeworldzLogoDataURL + `" alt="HomeWorldz"><p>Welcome to the HomeWorldz development grid.</p></main></body>
</html>`))
}

func (a *API) logo(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Content-Type", "image/svg+xml; charset=utf-8")
	w.Header().Set("Cache-Control", "public, max-age=3600")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(homeworldzLogo)
}
