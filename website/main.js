// nerx website: syntax colors, the typing hero, and the example tabs.

const KEYWORDS = new Set(("be lock print sh ask if else loop for in not and or func return type own init " +
  "options option try catch throw break continue exit im ex from lib ffi as delete true false errlang").split(" "));

function esc(s) {
  return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

// one line-based pass: strings, comments, numbers, words
function highlight(src) {
  let out = "";
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === '"') {
      let j = i + 1;
      while (j < src.length && src[j] !== '"' && src[j] !== "\n") j += src[j] === "\\" ? 2 : 1;
      j = Math.min(j + 1, src.length);
      out += '<span class="tk-str">' + esc(src.slice(i, j)) + "</span>";
      i = j;
    } else if (c === "#") {
      let j = src.indexOf("\n", i);
      if (j < 0) j = src.length;
      out += '<span class="tk-com">' + esc(src.slice(i, j)) + "</span>";
      i = j;
    } else if (/[0-9]/.test(c) && !/[A-Za-z_]/.test(src[i - 1] || "")) {
      let j = i;
      while (j < src.length && /[0-9.]/.test(src[j])) j++;
      out += '<span class="tk-num">' + src.slice(i, j) + "</span>";
      i = j;
    } else if (/[A-Za-z_]/.test(c)) {
      let j = i;
      while (j < src.length && /[A-Za-z0-9_]/.test(src[j])) j++;
      const w = src.slice(i, j);
      const isField = src[i - 1] === ".";
      out += KEYWORDS.has(w) && !isField ? '<span class="tk-key">' + w + "</span>" : w;
      i = j;
    } else {
      out += esc(c);
      i++;
    }
  }
  return out;
}

document.querySelectorAll('pre[data-lang="nerx"]').forEach((pre) => {
  pre.dataset.src = pre.textContent;
  pre.innerHTML = highlight(pre.textContent);
});

// hero: type the program, then show the commands and output line by line
const heroSrc = document.getElementById("hero-src");
const heroOut = document.getElementById("hero-out");
const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

if (heroSrc && heroOut && !reduceMotion) {
  const full = heroSrc.dataset.src;
  const outLines = heroOut.innerHTML.split("\n");
  let timer = null;

  function play() {
    clearTimeout(timer);
    heroOut.innerHTML = "";
    let n = 0;
    const step = () => {
      n = Math.min(full.length, n + 3);
      heroSrc.innerHTML = highlight(full.slice(0, n)) + (n < full.length ? '<span class="caret"></span>' : "");
      if (n < full.length) {
        timer = setTimeout(step, 16);
      } else {
        showOutput(0);
      }
    };
    step();
  }

  function showOutput(k) {
    heroOut.innerHTML = outLines.slice(0, k + 1).join("\n");
    if (k + 1 < outLines.length) {
      // commands pause a little longer than output lines
      const next = outLines[k + 1].startsWith('<span class="cmd">') ? 420 : 110;
      timer = setTimeout(() => showOutput(k + 1), next);
    } else {
      // type it again after 15 seconds; off screen, it waits until the box is visible
      timer = setTimeout(() => (visible ? play() : (pending = true)), 15000);
    }
  }

  let visible = false;
  let started = false;
  let pending = false;
  const io = new IntersectionObserver((entries) => {
    visible = entries[0].isIntersecting;
    if (visible && (!started || pending)) {
      started = true;
      pending = false;
      play();
    }
  });
  io.observe(heroSrc);
}

// tabs: click, arrow keys, Home/End
const tabs = Array.from(document.querySelectorAll('[role="tab"]'));
function select(tab) {
  tabs.forEach((t) => {
    const on = t === tab;
    t.setAttribute("aria-selected", on);
    t.tabIndex = on ? 0 : -1;
    document.getElementById(t.getAttribute("aria-controls")).hidden = !on;
  });
}
tabs.forEach((tab, i) => {
  tab.addEventListener("click", () => select(tab));
  tab.addEventListener("keydown", (e) => {
    let j = null;
    if (e.key === "ArrowRight") j = (i + 1) % tabs.length;
    if (e.key === "ArrowLeft") j = (i - 1 + tabs.length) % tabs.length;
    if (e.key === "Home") j = 0;
    if (e.key === "End") j = tabs.length - 1;
    if (j === null) return;
    e.preventDefault();
    select(tabs[j]);
    tabs[j].focus();
  });
});
