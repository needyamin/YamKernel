# YamBrowser vs Firefox Gap

This file is intentionally blunt. Firefox is a production browser engine and
YamBrowser is currently an early native browser path inside YamOS.

## What YamBrowser Has Now

- Kernel e1000 networking path in QEMU.
- DHCP, DNS, TCP, and plain HTTP/1.0 GET.
- Address bar, Go, reload, back/forward history, status line, and redirects for
  plain HTTP targets.
- Header/body separation so HTTP status text is not treated as the page.
- Static document nodes for:
  - headings
  - paragraphs
  - list items
  - links
  - pre/code blocks
  - input placeholders
  - button text
  - image placeholders from `alt` or `src`
- Scrollable document viewport with keyboard/page scrolling and wheel-event
  handling when the compositor provides wheel events.
- First inline style handling for simple text color and background color.
- Case-insensitive handling for common HTML tags.
- Script/style bodies are skipped instead of being printed as page text.
- HTTP error/status pages are still rendered as documents when they include a
  response body.

## What Firefox Has That YamBrowser Does Not

- HTTPS/TLS, certificate validation, HSTS, mixed-content policy.
- Full HTTP/1.1 and HTTP/2/3 behavior, compression, streaming, cache, cookies.
- HTML5 tokenizer and tree builder with standards-compatible error recovery.
- DOM tree, mutation model, events, forms, focus, selection, accessibility.
- CSS parser, cascade, selectors, inheritance, media queries, computed style.
- Layout engines for block, inline, flex, grid, table, positioned, overflow, and
  scrolling content.
- JavaScript engine, Web APIs, timers, promises, fetch/XHR, workers, modules.
- Image codecs, font loading, font shaping, canvas, SVG, video/audio.
- Multi-process sandboxing, site isolation, permissions, storage partitions.
- Developer tools, networking panel, console, performance tools.

## Current Honest Status

YamBrowser is no longer a fake offline app and no longer only a raw HTTP-status
viewer. It is a plain-HTTP fetcher with a static document paint layer.

It is still not a general web browser. It will not render modern HTTPS,
JavaScript-heavy, CSS-heavy websites until the missing engine and kernel
services are implemented.

## Next Production Steps

1. Add socket syscalls so browser networking moves out of ad hoc kernel helper
   calls and into a normal userspace service/app model.
2. Implement TLS and a certificate store before claiming modern website access.
3. Replace the tag scanner with an HTML tokenizer and tree builder.
4. Store a real DOM-like tree instead of flat render nodes.
5. Replace the current inline-style color sniffing with a CSS tokenizer/parser
   and basic cascade for element, class, id, and inline styles.
6. Replace the current flat block layout with real block and inline layout
   boxes.
7. Add image decoding placeholders first, then PNG/JPEG decoders.
8. Add a JavaScript runtime only after process isolation, memory accounting,
   timers, and event delivery are strong enough.
