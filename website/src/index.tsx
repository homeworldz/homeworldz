import { render } from "solid-js/web";
import { Route, Router } from "@solidjs/router";
import "vitre-css/vitre-base.css";
import "vitre-css/vitre.css";
import { Vitre } from "vitre-js";
import "./styles.css";
import { App } from "./App";
import { ArchitecturePage } from "./pages/ArchitecturePage";
import { LandingPage } from "./pages/LandingPage";
import { MarkdownPage } from "./pages/MarkdownPage";
import roadmap from "../../docs/ROADMAP.md?raw";

const root = document.getElementById("root");

if (!root) {
  throw new Error("The application root element is missing.");
}

render(
  () => (
    <Router root={App}>
      <Route path="/" component={LandingPage} />
      <Route path="/roadmap" component={() => <MarkdownPage markdown={roadmap} />} />
      <Route path="/architecture" component={ArchitecturePage} />
    </Router>
  ),
  root,
);

Vitre.apply();
