import { A } from "@solidjs/router";
import homeworldzMark from "./assets/brand/homeworldz-mark.svg";

export function App(props) {
  const closeMobileNavigation = (event) => {
    event.currentTarget.closest("details")?.removeAttribute("open");
  };

  return (
    <>
      <header class="site-header">
        <nav aria-label="Primary navigation">
          <div class="nav-start">
            <details class="mobile-nav">
              <summary aria-label="Open navigation menu">
                <span class="hamburger-icon" aria-hidden="true"></span>
              </summary>
              <div class="mobile-nav-panel">
                <A href="/architecture" onClick={closeMobileNavigation}>Architecture</A>
                <A href="/faq" onClick={closeMobileNavigation}>FAQ</A>
                <A href="/roadmap" onClick={closeMobileNavigation}>Roadmap</A>
              </div>
            </details>
            <A href="/" end class="brand" aria-label="HomeWorldz home">
              <img src={homeworldzMark} alt="" width="64" height="64" />
              <span>HomeWorldz</span>
            </A>
          </div>
          <div class="nav-controls">
            <div class="nav-links">
              <A href="/architecture">Architecture</A>
              <A href="/faq">FAQ</A>
              <A href="/roadmap">Roadmap</A>
            </div>
            <span data-kind="theme-toggle" aria-label="Choose color theme"></span>
          </div>
        </nav>
      </header>

      <main>{props.children}</main>
    </>
  );
}
