# website

Static site for nrxl. No build step except the docs page.

- `index.html`, `style.css`, `main.js`: home page.
- `docs.html`: generated from `../SYNTAX.en.txt` by `build_docs.ps1` using
  `docs_template.html`. After changing SYNTAX.en.txt (Windows):

      powershell -ExecutionPolicy Bypass -File website\build_docs.ps1

- `examples/`: the .nrx files shown on the page. Every output on the page came
  from running these with `nrxc run`. If the language changes, run them again
  and update the page.
- Preview locally: open index.html directly (some viewers skip the CSS; any static
  file server works too). It can be hosted as is on GitHub Pages or Cloudflare
  Pages.

## Design decisions (direction in ../DESIGN.md)

- Palette from DESIGN.md (kantor.nrx colors). Fixed light theme: the paper
  palette is the brand, chosen by the owner. All text pairs checked for WCAG AA
  (lowest: white on accent-dark button 4.53, accent on code box 4.79).
- Type: IBM Plex Sans for text, IBM Plex Mono only for code, self-hosted in
  fonts/ (SIL OFL 1.1, fonts/OFL.txt; weights 400/600/700, latin). Plex has engineered,
  precise letterforms, which fits "fast and sharp"; one family keeps the two
  faces consistent.
- The code box is the only dark surface, so the eye goes to the code first.
- Orange is used for language keywords, the primary button, and the "build" bar
  in the speed chart. Nowhere else.
- Motion: the hero code types once (3 characters per 16 ms, then output line by
  line), with a "Type it again" button. Nothing else moves. With
  prefers-reduced-motion the code is shown in full and nothing types.
- Speed numbers: measured 2026-11-04 on an Intel Xeon E3-1220 v2, wall time
  including process start, best of 3 (script) and 5 (.exe) runs, from
  examples/bench_fib.nrx and examples/bench_loop.nrx. Re-measure before
  changing them.
- nerx vs C numbers (section #vs-c): measured 2026-11-16 on an Intel Core
  i7-14700F, timed inside the program, C built with gcc -O2 (fib also -O0);
  sources in examples/cmp_*.c and examples/cmp_*.nrx.
