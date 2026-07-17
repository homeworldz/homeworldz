export function ArchitecturePage() {
  return (
    <article class="architecture-page">
      <header class="architecture-intro">
        <p class="eyebrow">Architecture</p>
        <h1>Shared services at the center. Independent worlds at the edges.</h1>
        <p class="lede">
          HomeWorldz separates the services that make a grid feel connected from the servers that
          run each world. The grid coordinates identity, inventory, discovery, and movement between
          regions. Region nodes own the live simulation and the content their regions need.
        </p>
      </header>

      <section aria-labelledby="system-shape-title">
        <div class="section-heading">
          <p class="eyebrow">System shape</p>
          <h2 id="system-shape-title">One grid can connect many independently operated region nodes.</h2>
        </div>

        <figure class="architecture-diagram" aria-labelledby="diagram-caption">
          <div class="architecture-diagram-scroll" tabindex="0" aria-label="Scrollable architecture diagram">
            <svg class="architecture-diagram-svg" viewBox="0 0 1120 700" role="img" aria-labelledby="architecture-svg-title architecture-svg-description">
              <title id="architecture-svg-title">HomeWorldz grid and region architecture</title>
              <desc id="architecture-svg-description">
                A central Go grid service stores durable data in PostgreSQL and coordinates three
                independent region hosts. Firestorm viewers use the grid for sign-in and discovery,
                then connect directly to a region for the live world experience.
              </desc>

              <defs>
                <marker id="arrow-grid" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
                  <path d="M 0 0 L 10 5 L 0 10 z" />
                </marker>
                <marker id="arrow-live" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
                  <path d="M 0 0 L 10 5 L 0 10 z" />
                </marker>
              </defs>

              <g class="diagram-links">
                <path class="diagram-link diagram-link-storage" d="M 180 410 V 335" />
                <text class="diagram-link-label" x="195" y="378">durable grid state</text>

                <path class="diagram-link diagram-link-grid" d="M 320 245 H 370 V 105 H 985" />
                <path class="diagram-link diagram-link-grid" d="M 525 105 V 150" />
                <path class="diagram-link diagram-link-grid" d="M 755 105 V 150" />
                <path class="diagram-link diagram-link-grid" d="M 985 105 V 150" />
                <text class="diagram-link-label diagram-link-label-grid" x="677" y="82">authenticated coordination · discovery · handoff</text>

                <path class="diagram-link diagram-link-login" d="M 420 615 H 365 V 285 H 320" />
                <path class="diagram-link diagram-link-login" d="M 650 615 H 365" />
                <path class="diagram-link diagram-link-login" d="M 880 615 H 365" />
                <text class="diagram-link-label diagram-link-label-login" x="380" y="550">sign in &amp; find destination</text>

                <path class="diagram-link diagram-link-live" d="M 525 410 V 580" />
                <path class="diagram-link diagram-link-live" d="M 755 410 V 580" />
                <path class="diagram-link diagram-link-live" d="M 985 410 V 580" />
                <text class="diagram-link-label diagram-link-label-live" x="770" y="505">direct live world connections</text>
              </g>

              <g class="diagram-card diagram-card-grid">
                <rect x="40" y="170" width="280" height="165" rx="16" />
                <text class="diagram-svg-kicker" x="64" y="202">CENTRAL GRID HOST</text>
                <text class="diagram-svg-title" x="64" y="240">Grid services</text>
                <text class="diagram-svg-copy" x="64" y="271">Identity · inventory · presence</text>
                <text class="diagram-svg-copy" x="64" y="295">Registry · map · coordination</text>
                <text class="diagram-svg-tech" x="64" y="319">GO</text>
              </g>

              <g class="diagram-card diagram-card-database">
                <rect x="70" y="410" width="220" height="120" rx="16" />
                <text class="diagram-svg-kicker" x="94" y="442">GRID DATA</text>
                <text class="diagram-svg-title diagram-svg-title-small" x="94" y="480">PostgreSQL</text>
                <text class="diagram-svg-copy" x="94" y="508">Accounts · inventory · presence</text>
              </g>

              <g class="diagram-card diagram-card-region diagram-card-owner-one">
                <rect x="420" y="150" width="210" height="260" rx="16" />
                <text class="diagram-svg-kicker" x="440" y="181">REMOTE REGION NODE</text>
                <g class="diagram-owner">
                  <line x1="440" y1="201" x2="458" y2="201" />
                  <text x="466" y="205">OWNER 1</text>
                </g>
                <text class="diagram-svg-title diagram-svg-title-small" x="440" y="235">Welcome host</text>
                <g class="diagram-process">
                  <rect x="440" y="250" width="170" height="78" rx="12" />
                <text class="diagram-process-title" x="456" y="280">Welcome region</text>
                  <text class="diagram-svg-copy" x="456" y="306">C++ · Jolt · local state</text>
                </g>
                <g class="diagram-storage">
                  <line x1="440" y1="365" x2="610" y2="365" />
                  <text x="440" y="391">Assets</text>
                </g>
              </g>

              <g class="diagram-card diagram-card-region diagram-card-owner-two">
                <rect x="650" y="150" width="210" height="260" rx="16" />
                <text class="diagram-svg-kicker" x="670" y="181">REMOTE REGION NODE</text>
                <g class="diagram-owner">
                  <line x1="670" y1="201" x2="688" y2="201" />
                  <text x="696" y="205">OWNER 2</text>
                </g>
                <text class="diagram-svg-title diagram-svg-title-small" x="670" y="235">Community host</text>
                <g class="diagram-process">
                  <rect x="670" y="250" width="170" height="48" rx="12" />
                  <text class="diagram-process-title" x="686" y="270">Events region</text>
                  <text class="diagram-svg-copy" x="686" y="289">C++ · Jolt · local state</text>
                </g>
                <g class="diagram-process">
                  <rect x="670" y="306" width="170" height="48" rx="12" />
                  <text class="diagram-process-title" x="686" y="326">Sandbox region</text>
                  <text class="diagram-svg-copy" x="686" y="345">C++ · Jolt · local state</text>
                </g>
                <g class="diagram-storage">
                  <line x1="670" y1="365" x2="840" y2="365" />
                  <text x="670" y="391">Assets</text>
                </g>
              </g>

              <g class="diagram-card diagram-card-region diagram-card-owner-three">
                <rect x="880" y="150" width="210" height="260" rx="16" />
                <text class="diagram-svg-kicker" x="900" y="181">REMOTE REGION NODE</text>
                <g class="diagram-owner">
                  <line x1="900" y1="201" x2="918" y2="201" />
                  <text x="926" y="205">OWNER 3</text>
                </g>
                <text class="diagram-svg-title diagram-svg-title-small" x="900" y="235">Personal host</text>
                <g class="diagram-process">
                  <rect x="900" y="250" width="170" height="78" rx="12" />
                  <text class="diagram-process-title" x="916" y="280">Home region</text>
                  <text class="diagram-svg-copy" x="916" y="306">C++ · Jolt · local state</text>
                </g>
                <g class="diagram-storage">
                  <line x1="900" y1="365" x2="1070" y2="365" />
                  <text x="900" y="391">Assets</text>
                </g>
              </g>

              <g class="diagram-viewer">
                <g><rect x="420" y="580" width="210" height="70" rx="14" /><circle cx="480" cy="615" r="7" /><text x="498" y="621">Viewer(s)</text></g>
                <g><rect x="650" y="580" width="210" height="70" rx="14" /><circle cx="710" cy="615" r="7" /><text x="728" y="621">Viewer(s)</text></g>
                <g><rect x="880" y="580" width="210" height="70" rx="14" /><circle cx="940" cy="615" r="7" /><text x="958" y="621">Viewer(s)</text></g>
              </g>
            </svg>
          </div>

          <figcaption id="diagram-caption">
            Viewers use the grid to log in and find a destination, then connect to the region that
            runs that part of the world. A region node can run one or more separate region processes.
          </figcaption>
        </figure>
      </section>

      <section class="connection-flow" aria-labelledby="connection-title">
        <div class="section-heading">
          <p class="eyebrow">A viewer enters the world</p>
          <h2 id="connection-title">
            The <strong>grid</strong> introduces the <strong>viewer</strong> → the <strong>region</strong> runs the <em>experience</em>.
          </h2>
        </div>
        <ol>
          <li>
            <strong>Sign in</strong>
            <span>Firestorm contacts the grid, which authenticates the account and resolves a destination.</span>
          </li>
          <li>
            <strong>Connect</strong>
            <span>The viewer establishes a direct circuit with the destination region.</span>
          </li>
          <li>
            <strong>Enter</strong>
            <span>The region streams terrain, objects, avatars, movement, physics, and capabilities.</span>
          </li>
          <li>
            <strong>Move between worlds</strong>
            <span>The grid and regions coordinate teleports without making the grid run the simulation.</span>
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
            <h3>The grid connects the world</h3>
            <p>
              Central services maintain identities, inventory metadata, online presence, region
              discovery, the world map, and coordination between independently running regions.
            </p>
            <p class="boundary-store"><strong>Durable state:</strong> PostgreSQL</p>
          </article>
          <article>
            <h3>Regions run the world</h3>
            <p>
              Each region owns its scene, terrain, objects, avatar simulation, physics, viewer
              updates, and local asset bytes. A grid restart does not stop a running region simulation.
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
              Immutable asset bytes live near the regions that use them. Regions can discover and
              verify copies without turning the grid into one central asset warehouse.
            </p>
          </article>
          <article>
            <h3>Live world maps</h3>
            <p>
              The grid renders map tiles from running regions’ current terrain, so viewer terrain
              edits can reach the world map without a separate daily map-generation job.
            </p>
          </article>
          <article>
            <h3>Restartable central services</h3>
            <p>
              Grid services and region simulations have separate lifecycles. Active regions keep
              simulating through a brief central grid restart and resume shared operations afterward.
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
