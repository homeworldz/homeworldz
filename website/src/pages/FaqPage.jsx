export function FaqPage() {
  return (
    <article class="faq-page">
      <header class="faq-intro">
        <p class="eyebrow">Frequently asked questions</p>
        <p class="lede">
          The short answers to how regions are operated, where world data lives, and what the
          experience looks like to visitors.
        </p>
      </header>

      <nav class="faq-toc" aria-labelledby="faq-toc-title">
        <h2 id="faq-toc-title">On this page</h2>
        <ul>
          <li><a href="#what-is-homeworldz">What the heck is this?</a></li>
          <li><a href="#supported-viewers">Which viewers are supported?</a></li>
          <li><a href="#visitor-experience">What do visitors see?</a></li>
          <li><a href="#grid-operation">Do I need to operate an entire grid?</a></li>
          <li><a href="#multiple-regions">Can one host run more than one region?</a></li>
          <li><a href="#asset-storage">Where are assets stored? Can I back up my region?</a></li>
          <li><a href="#grid-restarts">What happens if the central grid restarts?</a></li>
          <li><a href="#pricing-short-answer">What is the short answer on pricing?</a></li>
          <li><a href="#pricing-long-answer">What is the longer answer on pricing?</a></li>
          <li><a href="#availability">When can I try this? How is progress advancing?</a></li>
          <li><a href="#run-a-region">Is there an installer I can try? How do I run my own region?</a></li>
          <li><a href="#low-grid-prices">How are you able to offer grid services at such low prices?</a></li>
          <li><a href="#intellectual-property">How is intellectual property protected?</a></li>
        </ul>
      </nav>

      <div class="faq-list" aria-label="HomeWorldz questions and answers">
        <section class="faq-item" id="what-is-homeworldz">
          <h2>What the heck is this?</h2>
          <p>
            It is an easy way to run an SL-like virtual world on your own machine.
            You can run it at home, or pay for an inexpensive VPS machine on a cloud server like OVH or Digital Ocean.
          </p>
          <p>
            When hooked up to the central public grid, and marked not as a restricted-access private region, visitors
            with any Firestorm-compatible viewer can visit your region.
          </p>
        </section>

        <section class="faq-item" id="supported-viewers">
          <h2>Which viewers are supported?</h2>
          <p>
            Firestorm compatibility is the first target. HomeWorldz presents the familiar viewer
            protocols at the edge while using a new server design internally.
          </p>
        </section>

        <section class="faq-item" id="visitor-experience">
          <h2>What do visitors see?</h2>
          <p>
            Visitors use a compatible viewer to sign in, find destinations, and enter a region.
            They see the terrain, objects, avatars, and experiences created by that region’s owner,
            with familiar inventory, map, movement, and building workflows.
          </p>
          <p>
            It looks and feels the same, except that some of the old quirks are resolved
            by a fresh implementation. Avatar clouds,rebakes, etc are all brand-new and shiny
            and have that "new car smell".
          </p>
        </section>

        <section class="faq-item" id="grid-operation">
          <h2>Do I need to operate an entire grid?</h2>
          <p>
            No. A region operator can connect a node to an existing grid and remain responsible
            only for their own regions, content, and local infrastructure. Grid services are a
            separate operational role.
          </p>
        </section>

        <section class="faq-item" id="multiple-regions">
          <h2>Can one host run more than one region?</h2>
          <p>
            Yes. One region node can run multiple independent region processes. Operators can keep
            a small deployment together on one host or spread larger worlds across several hosts.
          </p>
        </section>

        <section class="faq-item" id="asset-storage">
          <h2>Where are assets stored? Can I back up my region?</h2>
          <p>
            Regions can discover and verify copies without relying on one central asset warehouse.
            Assets live on the machines with the regions that use them, on storage controlled by
            each region operator. (In English, this means they are stored with your region at your home, or where you choose.)
          </p>
          <p>
            The grid stores shared records and inventory metadata in PostgreSQL, while region-local
            scenes, assets, and simulation state remain at the edge.
          </p>
          <p>
            <strong>This also means that saving a full backup of your region and everything in it, <em>including assets,</em> is as easy as copying
              the HomeWorldz region folder somewhere safe, yourself.</strong>
          </p>
        </section>

        <section class="faq-item" id="grid-restarts">
          <h2>What happens if the central grid restarts?</h2>
          <p>
            Running regions keep simulating because grid services and region processes have separate
            lifecycles. Shared operations such as login, discovery, and teleport coordination resume
            after the grid is available again.
          </p>
        </section>

        <section class="faq-item" id="pricing-short-answer">
          <h2>What is the short answer on pricing?</h2>
          <p>
            Since you will be running <em>your own regions</em> on <em>your own machines</em>, that part
            is <strong>free</strong>, or whatever you want to pay for a machine of your choosing.
          </p>
          <p>
            <em>Registering</em> on the central public grid uses grid resources which will start adding up pretty quickly,
            so <em>registration</em> of new regions won't be <em>completely</em> free.
            Current expectations are no more than $5/month, or something like that. Pricing to be determined. More below in the longer answer.
          </p>
          <p>
            Remember, you are hosting the region, this is just the registration to connect them all together,
            to provide centralized services like user, inventory and teleport management, etc. We want to encourage wide use.
          </p>
        </section>

        <section class="faq-item" id="pricing-long-answer">
          <h2>What is the longer answer on pricing?</h2>
          <p>
            Since you will be running <em>your own regions</em> on <em>your own machines</em>, that part
            is <strong>free</strong>, or whatever you want to pay for a machine of your choosing.
          </p>
          <p>
            That said, <em>registering</em> on the central public grid uses grid resources which will start adding up pretty quickly,
            so <em>registration</em> of new regions won't be <em>completely</em> free.
          </p>
          <p>
            Final pricing for HomeWorldz region registration has not yet been determined, but the current expectations,
            subject to change as it firms up, places registration cost at maximum of $5 per region.
            It may end up perhaps in cheaper bundles of $5 per <em>bundle of several region registrations</em>.
            Remember, you are hosting the region, this is just the registration to connect them all together,
            to provide centralized services like user, inventory and teleport management, etc. We want to encourage wide use.
          </p>
          <p>
            Running your own region will also involve your own infrastructure costs for compute, storage,
            and bandwidth, which depend on the size and activity of your world and the hosting provider you choose.
          </p>
          <p>
            That said, your infrastructure may be running <em>in your own home</em>, for the cost of electricity.
            It is up to you to decide how serious you want to be, and a free self-hosted region running
            from home is expected to be the norm. After all, this is the origin of the name "HomeWorldz".
          </p>
          <p>
            Because regions run on infrastructure you control, you can choose a deployment that
            matches your budget and scale it as your community grows. The only other cost is the nominal
            cost of registration with the grid.
          </p>
        </section>

        <section class="faq-item" id="availability">
          <h2>When can I try this? How is progress advancing?</h2>
          <p>
            It is not available to download or try yet, but something to try should be available
            in a matter of weeks, not months. You will probably be able to register a Homeworldz
            account within a week or two, and try it on our hosted development test regions before
            wider hookups are offered.
          </p>
          <p>
            Progress has been epic. Still in <em>the first week</em>, and written entirely from scratch,
            it is already possible to login to one or more regions with the Firestorm viewer,
            where it is already possible to see some improvements over Halcyon and OpenSim servers.
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
            Because of this, it is too early to estimate completion but it is safe to say the current
            pace of progress is extreme. You can follow along on the <a href="/roadmap">Roadmap page</a>.
          </p>
        </section>


        <section class="faq-item" id="run-a-region">
          <h2>Is there an installer I can try? How do I run my own region?</h2>
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
        </section>

        <section class="faq-item" id="low-grid-prices">
          <h2>How will you offer grid services at such low prices?</h2>
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
        </section>

        <section class="faq-item" id="intellectual-property">
          <h2>How is intellectual property protected?</h2>
          <p>
            Because assets are stored under the direction of the region owner, there are legitimate
            concerns about Intellectual Property rights and protections. But by completely redesigning
            the architecture and implementation, HomeWorldz does not come with the same limitations of
            SL-compatible grids based on OpenSim. This means, for example, assets themselves can hold
            much more data than in the past, including the original creator/uploader, as many nested
            levels of permissions as desired, specialized permissions designed for distributed assets,
            and a full provenance chain of where this asset has been. Most use cases won't need anything
            like this, and in fact the original design of Homeworldz years ago resolved this by
            restricting permissions to always be permissive (full-perm), and abandoning the whole
            permissions system.
          </p>
          <p>
            Some of this still needs to be worked out, but even just storing the creator in the asset
            goes a long way to regaining control for creators. Saving creator intentions in an asset
            adds permissions on steroids, and layering a provenance chain creates opportunities for
            creator control that have not been heard of before.
          </p>
          <p>
            HomeWorldz believes that a grid must either provide reliable IP protections or move them out
            of the way don't pretend there is a strong permissions system.
          </p>
        </section>

      </div>
    </article>
  );
}
