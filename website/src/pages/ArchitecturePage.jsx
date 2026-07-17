export function ArchitecturePage() {
  return (
    <article class="architecture-page">
      <header class="architecture-intro">
        <p class="eyebrow">Architecture</p>
        <h1>Shared services at the center. Independent worlds at the edges.</h1>
        <p class="lede">
          HomeWorldz separates the services that make a grid feel connected from the servers that
          run each world. The Grid coordinates identity, inventory, discovery, and movement between
          regions. Region nodes own the live simulation and the content their regions need.
        </p>
      </header>

      <section aria-labelledby="system-shape-title">
        <div class="section-heading">
          <p class="eyebrow">System shape</p>
          <h2 id="system-shape-title">One grid can connect many independently operated region nodes.</h2>
        </div>

        <figure class="architecture-diagram" aria-labelledby="diagram-caption">
          <div class="diagram-grid-service">
            <span class="diagram-kicker">Central grid host</span>
            <h3>Grid services</h3>
            <p>Identity · inventory · presence · region registry · map · coordination</p>
            <strong>Go</strong>
          </div>

          <div class="diagram-storage-link" aria-hidden="true">
            <span>durable grid state</span>
          </div>

          <div class="diagram-database">
            <span class="diagram-kicker">Grid data</span>
            <h3>PostgreSQL</h3>
            <p>Accounts, inventory metadata, sessions, presence, and region records</p>
          </div>

          <div class="diagram-coordination" aria-hidden="true">
            <span>authenticated coordination, discovery, and handoff</span>
          </div>

          <div class="region-fleet" aria-label="Remote region nodes">
            <section class="region-node">
              <span class="diagram-kicker">Remote region node</span>
              <h3>Welcome host</h3>
              <div class="region-process">
                <strong>Welcome Region</strong>
                <span>C++ · Jolt · local state</span>
              </div>
            </section>

            <section class="region-node region-node-multiple">
              <span class="diagram-kicker">Remote region node</span>
              <h3>Community host</h3>
              <div class="region-process">
                <strong>Events Region</strong>
                <span>C++ · Jolt · local state</span>
              </div>
              <div class="region-process">
                <strong>Sandbox Region</strong>
                <span>C++ · Jolt · local state</span>
              </div>
            </section>

            <section class="region-node">
              <span class="diagram-kicker">Remote region node</span>
              <h3>Personal host</h3>
              <div class="region-process">
                <strong>Home Region</strong>
                <span>C++ · Jolt · local state</span>
              </div>
            </section>
          </div>

          <div class="diagram-viewer-link" aria-hidden="true">
            <span>live world connections</span>
          </div>

          <div class="viewer-fleet" aria-label="Firestorm viewers">
            <span>Firestorm viewer</span>
            <span>Firestorm viewer</span>
            <span>Firestorm viewer</span>
          </div>

          <figcaption id="diagram-caption">
            Viewers use the Grid to log in and find a destination, then connect to the Region that
            runs that part of the world. A region node can run one or more separate Region processes.
          </figcaption>
        </figure>
      </section>

      <section class="connection-flow" aria-labelledby="connection-title">
        <div class="section-heading">
          <p class="eyebrow">A viewer enters the world</p>
          <h2 id="connection-title">The central Grid introduces the viewer. The Region runs the experience.</h2>
        </div>
        <ol>
          <li>
            <strong>Sign in</strong>
            <span>Firestorm contacts the Grid, which authenticates the account and resolves a destination.</span>
          </li>
          <li>
            <strong>Connect</strong>
            <span>The viewer establishes a direct circuit with the destination Region.</span>
          </li>
          <li>
            <strong>Enter</strong>
            <span>The Region streams terrain, objects, avatars, movement, physics, and capabilities.</span>
          </li>
          <li>
            <strong>Move between worlds</strong>
            <span>The Grid and Regions coordinate teleports without making the Grid run the simulation.</span>
          </li>
        </ol>
      </section>

      <section aria-labelledby="boundaries-title">
        <div class="section-heading">
          <p class="eyebrow">Clear ownership</p>
          <h2 id="boundaries-title">Each part of the system has a focused job.</h2>
        </div>
        <div class="boundary-grid">
          <article>
            <h3>The Grid connects the world</h3>
            <p>
              Central services maintain identities, inventory metadata, online presence, region
              discovery, the world map, and coordination between independently running Regions.
            </p>
            <p class="boundary-store"><strong>Durable state:</strong> PostgreSQL</p>
          </article>
          <article>
            <h3>Regions run the world</h3>
            <p>
              Each Region owns its scene, terrain, objects, avatar simulation, physics, viewer
              updates, and local asset bytes. A Grid restart does not stop a running Region simulation.
            </p>
            <p class="boundary-store"><strong>Durable state:</strong> local SQLite and files</p>
          </article>
          <article>
            <h3>Viewers see compatibility</h3>
            <p>
              Firestorm receives the familiar login, inventory, map, movement, building, and world
              protocols it expects. The internal server design does not reproduce legacy internals.
            </p>
            <p class="boundary-store"><strong>Compatibility edge:</strong> viewer protocols</p>
          </article>
        </div>
      </section>

      <section class="difference-section" aria-labelledby="different-title">
        <div class="section-heading">
          <p class="eyebrow">Different by design</p>
          <h2 id="different-title">Modern internals support a familiar viewer experience.</h2>
        </div>
        <div class="difference-list">
          <article>
            <h3>Region-local assets</h3>
            <p>
              Immutable asset bytes live near the Regions that use them. Regions can discover and
              verify copies without turning the Grid into one central asset warehouse.
            </p>
          </article>
          <article>
            <h3>Live world maps</h3>
            <p>
              The Grid renders map tiles from running Regions’ current terrain, so viewer terrain
              edits can reach the world map without a separate daily map-generation job.
            </p>
          </article>
          <article>
            <h3>Restartable central services</h3>
            <p>
              Grid services and Region simulations have separate lifecycles. Active Regions keep
              simulating through a brief central Grid restart and resume shared operations afterward.
              A central grid restart does not require the entire grid to be restarted.
            </p>
          </article>
          <article>
            <h3>Pluggable physics</h3>
            <p>
              The scene model is not owned by a physics engine. Jolt powers the current production
              path behind a boundary that keeps authoritative world state portable to other physics
              engines. HomeWorldz will provide an optional alternative native NVIDIA PhysX 5 reference
              implementation as well as the standard Jolt-based option.
            </p>
          </article>
        </div>
      </section>
    </article>
  );
}
