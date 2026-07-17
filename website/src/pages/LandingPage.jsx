import { A } from "@solidjs/router";
import homeworldzLogo from "../assets/brand/homeworldz.svg";

export function LandingPage() {
  return (
    <>
      <aside class="coming-soon-banner" aria-label="Availability notice">
        <strong>Coming soon</strong>
        <span>HomeWorldz is under active development and is not available yet.</span>
      </aside>

      <section class="hero" aria-labelledby="hero-title">
        <div>
          <p class="eyebrow">A new foundation for building open virtual worlds</p>
          <h1 id="hero-title">Virtual worlds should feel boundless. Their infrastructure shouldn’t.</h1>
          <p class="lede">
            Instead, it should define clear boundaries. HomeWorldz is a clean-architecture
            virtual world server built for practical viewer compatibility,{" "}
            <em class="key-emphasis">independently-operated</em> regions, and infrastructure you can
            understand. It provides the infrastructure framework, for you to <em>easily</em> run your own regions where desired.
          </p>
          <p class="actions">
            <A href="/roadmap" role="button">Explore the roadmap</A>
            <A href="/architecture" role="button" data-variant="outline">Read the architecture</A>
          </p>
        </div>
        <figure class="world-mark">
          <img src={homeworldzLogo} alt="HomeWorldz" />
          <figcaption>Your worlds. Clear boundaries.</figcaption>
        </figure>
      </section>

      <section aria-labelledby="direction-title">
        <p class="eyebrow">Project direction</p>
        <h2 id="direction-title">Designed around the world, not legacy internals.</h2>
        <div class="feature-grid">
          <article>
            <h3>Focused central grid</h3>
            <p>
              Central services coordinate identity, inventory, presence, discovery, and movement
              without absorbing the whole world.
            </p>
          </article>
          <article>
            <h3>Regions with agency</h3>
            <p>
              <em>Independently-operated</em> region servers own simulation, scenes, terrain, and the assets
              their worlds need.
            </p>
          </article>
          <article>
            <h3>Firestorm first</h3>
            <p>
              Practical compatibility with the viewer people already know, backed by a new server
              implementation rather than inherited service boundaries.
            </p>
          </article>
        </div>
      </section>

      <section class="technology" aria-labelledby="technology-title">
        <div>
          <p class="eyebrow">A Completely Modernized Tech Stack</p>
          <h2 id="technology-title">Optimal technology at every boundary.</h2>
        </div>
        <ul>
          <li><strong>C++20</strong> for optimal gaming simulation for regions</li>
          <li><strong>Jolt Physics</strong> default for powerful in-world physics</li>
          <li><strong>NVIDIA PhysX 5</strong> optional alternative physics</li>
          <li><strong>Go language</strong> for central grid services</li>
          <li><strong>PostgreSQL</strong> for the durable grid state</li>
        </ul>
      </section>
    </>
  );
}
