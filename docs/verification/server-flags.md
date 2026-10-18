# Server-sourced currency flags

## What was verified

Against `eager_maxwell` on 2026-10-18, signed in as
`tenant_admin@acme_corporation`:

- the runtime read `refdata.v1.currencies.list` once for the tenant;
- the chart's two currencies produced `load_image` commands carrying image
  identifiers, not currency codes;
- each read went to `assets.v1.images.list` filtered by `id_one_of`;
- the returned SVG bytes were rasterised with lunasvg and drawn in the chart
  header.

The header showed the server's Union Jack and United States flag, not the
client's drawn approximations. A 1920x1080 capture was taken with
`HEDGR_CAPTURE`, frame 700, with the chart focused.

## How the path fits together

1. `session_client::currency_images()` reads the refdata currency records and
   keeps `iso_code -> image_id` where the record names an image.
2. `tenant_image_map` holds that map per tenant.
3. A chart or series row asks for a currency flag through the map; the
   resulting `load_image` command carries the image identifier.
4. `image_worker` reads the image by identifier from the assets service and
   posts it; `image_cache` stores the bytes, keyed by tenant and identifier,
   bounded by bytes and evicted LRU.
5. The application uploads a texture per image into `image_texture_cache`,
   also keyed by tenant and identifier, with its own decoded-byte budget. A
   texture used in the current frame is pinned; unpinned textures are evicted
   at the end of the frame and deleted with the graphics context.
6. `draw_flag` uses the uploaded texture when there is one; otherwise it draws
   the client's flag primitives, which remain the fallback for a currency with
   no image or an image that cannot be decoded.

## Notes

- Image bytes never change for an identifier, so neither cache has a TTL.
- Image identifiers are tenant-scoped; the caches key by tenant and identifier.
- SVG is rasterised at 64x44, the largest flag the chart draws at twice the
  natural size. PNG and JPEG are decoded with stb.
- The live capture is an artefact of the run, not committed. The commands are
  the same as the other capture scripts, with an instance for `eager_maxwell`
  and the same credentials.
