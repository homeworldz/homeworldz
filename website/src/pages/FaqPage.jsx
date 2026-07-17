export function FaqPage() {
  return (
    <article class="faq-page">
      <header class="faq-intro">
        <p class="eyebrow">Frequently asked questions</p>
        <h1>Running worlds on HomeWorldz.</h1>
        <p class="lede">
          The short answers to how Regions are operated, where world data lives, and what the
          experience looks like to visitors.
        </p>
      </header>

      <section class="faq-list" aria-label="HomeWorldz questions and answers">
        <details name="faq">
          <summary>When can I try this?</summary>
          <p>
            It is not available to download or try yet, but something to try should be available
            in a matter of weeks, not months. You will probably be able to register a Homeworldz
            account within a week or two, and try it on our hosted development test regions.
          </p>
          <p>
            Progress has been epic. Still in <em>the first week</em>, it is already possible to
            login to one or more regions with the Firestorm viewer, where it is already possible
            to see improvements over Halcyon and OpenSim servers.
          </p>
          <p>
            Logins are complete with avatar initialization, a Library with defaults, appearance and
            Outfits support, terrain editing, with physics, avatar movement, flight and animations,
            support for multiple regions and teleports, building and editing with prims, including
            physical prims, physical prim interactions with avatars, inventory, rezzing and Take,
            and so many more core features of Second Life and Firestorm-compatible virtual worlds.
            Already test logins with Firestorm are fast, reliable and somewhat feature-rich, especially
            if you consider that the project is less than a week old.
          </p>
          <p>
            Because of this, it is too early to estimate completion but I think it is safe to say the
            pace of progress is extreme. You can follow along on the <a href="/roadmap">Roadmap page</a>.
          </p>
        </details>

        <details name="faq">
          <summary>Is there an installer I can try? How do I run my own Region?</summary>
          <p>
            We made it as simple as possible. Once the region installation bundle becomes available,
            for Windows, Mac and Linux, you will be able to download it and (almost) just run it.
          </p>
          <p>
            But before you do that, remember it is not a whole grid, it is a portable region on a shared grid.
            Your region or regions will need to <em>exist somewhere</em> on the map, and will need a region name,
            and an access key to allow it to connect to that shared grid.
          </p>
          <p>
            So the first step is to register your region on the HomeWorldz grid. You will be given a configuration
            file to copy into your HomeWorldz region folder, one for each region you wish to bring up.
          </p>
          <p>
            Then it is just an executable program you start like any other. Then you start your viewer and log in.
            Packaging and step-by-step setup will be published as the implementation reaches its first availability release.
          </p>
        </details>

        <details name="faq">
          <summary>How much does this cost?</summary>
          <p>
            Since you will be running <em>your own regions</em> on <em>your own machines</em>, that part
            is <strong>free</strong>. That said, <em>registering</em> on the grid uses central grid
            resources which will start adding up pretty quickly, so <em>registration</em> of new regions won't be free.
          </p>
          <p>
            Final pricing for HomeWorldz region registration has not yet been determined, but the current expectations,
            subject to change as it firms up, places registration cost at maximum of $5 per region.
            It may end up perhaps $5 per <em>bundle of many region registrations</em>.
            Remember, you are hosting the region, this is just the registration to connect them all together,
            to provide centralized services like user, inventory and teleport management, etc. We want to encourage wide use.
          </p>
          <p>
            Running your own region will involve your own
            infrastructure costs for compute, storage, and bandwidth, which depend on the size and
            activity of your world and the hosting provider you choose.
          </p>
          <p>
            That said, your infrastructure may be running in your home, for the cost of electricity.
            It is up to you to decide how serious you want to be, and a free self-hosted region running
            from home is expected to be the norm.
          </p>
          <p>
            Because Regions run on infrastructure you control, you can choose a deployment that
            matches your budget and scale it as your community grows. Any fees or terms for joining
            a Grid will be set by that Grid’s operator.
          </p>
        </details>

        <details name="faq">
          <summary>How are you able to offer grid services at such low prices?</summary>
          <p>
            One of the critical costs of running InWorldz and other Halcyon grids -- or for that matter
            OpenSim as well -- was the ever-growing size of asset storage. For every user that <em>ever</em>
            uploaded a texture, or a mesh, or wrote a script, those assets needed to be stored <em>forever</em>.
            When InWorldz moved to Rackspace hosting, the size of the assets was over 13TB of data. You may
            remember that the transfer of assets to InWorldz took weeks to perform, in large copy batches
            that ran day after day.
          </p>
          <p>
            This may be the biggest problem facing SL-like virtual worlds: an ever-increasing burden of asset storage.
            There have been some OpenSim attempts to reduce this through deduplication or asset expiry (very dangerous)
            but all of these approaches do not really solve the core problem.
          </p>
          <p>
             Running your own region with local storage of the assets needed to present that region to your viewers
             and visitors moves that problem such that it is suddenly associate with the region, not the grid. This
             means the grid doesn't really care, or need to prepare storage, for growing from 10 regions to 100,000.
             More importantly, it means when a user shuts down a region, the grid is no longer responsible for
             presenting those assets, which means no longer responsible for storing them. (In Homeworldz, it never was.)
          </p>
          <p>
            That said, your infrastructure may be running in your home, for the cost of electricity.
            It is up to you to decide how serious you want to be, and a free self-hosted region running
            from home is expected to be the norm.
          </p>
          <p>
            Because Regions run on infrastructure you control, you can choose a deployment that
            matches your budget and scale it as your community grows. Any fees or terms for joining
            a Grid will be set by that Grid’s operator.
          </p>
        </details>

        <details name="faq">
          <summary>Where are assets stored?</summary>
          <p>
            Immutable asset bytes live near the Regions that use them, on storage controlled by
            each Region operator. Regions can discover and verify copies without relying on one
            central asset warehouse.
          </p>
          <p>
            The Grid stores shared records and inventory metadata in PostgreSQL, while Region-local
            scenes, assets, and simulation state remain at the edge.
          </p>
        </details>

        <details name="faq">
          <summary>What do visitors see?</summary>
          <p>
            Visitors use a compatible viewer to sign in, find destinations, and enter a Region.
            They see the terrain, objects, avatars, and experiences created by that Region’s owner,
            with familiar inventory, map, movement, and building workflows.
          </p>
        </details>

        <details name="faq">
          <summary>Do I need to operate an entire Grid?</summary>
          <p>
            No. A Region operator can connect a node to an existing Grid and remain responsible
            only for their own Regions, content, and local infrastructure. Grid services are a
            separate operational role.
          </p>
        </details>

        <details name="faq">
          <summary>Can one host run more than one Region?</summary>
          <p>
            Yes. One Region node can run multiple independent Region processes. Operators can keep
            a small deployment together on one host or spread larger worlds across several hosts.
          </p>
        </details>

        <details>
          <summary>What happens if the central Grid restarts?</summary>
          <p>
            Running Regions keep simulating because Grid services and Region processes have separate
            lifecycles. Shared operations such as login, discovery, and teleport coordination resume
            after the Grid is available again.
          </p>
        </details>

        <details>
          <summary>Which viewers are supported?</summary>
          <p>
            Firestorm compatibility is the first target. HomeWorldz presents the familiar viewer
            protocols at the edge while using a new server design internally.
          </p>
        </details>
      </section>
    </article>
  );
}
