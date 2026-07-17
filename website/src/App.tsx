import type { ParentProps } from "solid-js";
import { A } from "@solidjs/router";

export function App(props: ParentProps) {
  return (
    <>
      <header class="site-header">
        <nav aria-label="Primary navigation">
          <A href="/" end class="brand" aria-label="HomeWorldz home">
            <img src="/homeworldz-mark.svg" alt="" />
            <span>HomeWorldz</span>
          </A>
          <div class="nav-links">
            <A href="/architecture">Architecture</A>
            <A href="/roadmap">Roadmap</A>
            <span data-kind="theme-toggle" aria-label="Choose color theme"></span>
          </div>
        </nav>
      </header>

      <main>{props.children}</main>
    </>
  );
}
