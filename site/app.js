// Démo dans le navigateur (émulateur v86) et dernière release.
// Rien d'injecté en HTML : tout passe par textContent / setAttribute, et les liens de
// téléchargement ne sont acceptés que s'ils pointent vers les releases du dépôt.
(function () {
  "use strict";

  var REPO = "abiyeenzo/lilos";
  var DOWNLOAD_PREFIX = "https://github.com/" + REPO + "/releases/download/";

  // ---------------------------------------------------------------- release
  function fmtSize(n) {
    if (n > 1048576) return (n / 1048576).toFixed(1).replace(".", ",") + " Mo";
    return Math.max(1, Math.round(n / 1024)) + " Ko";
  }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
  }

  function renderRelease(box, data) {
    box.textContent = "";
    var head = el("div", "release-head");
    head.appendChild(el("span", "tag", data.tag_name));
    var when = new Date(data.published_at);
    if (!isNaN(when)) {
      head.appendChild(el("span", "when", "publiée le " + when.toLocaleDateString("fr-FR", { day: "numeric", month: "long", year: "numeric" })));
    }
    if (data.prerelease) head.appendChild(el("span", "badge", "préversion"));
    box.appendChild(head);

    var files = el("div", "files");
    (data.assets || []).forEach(function (a) {
      if (typeof a.browser_download_url !== "string" || a.browser_download_url.indexOf(DOWNLOAD_PREFIX) !== 0) return;
      var row = el("div", "file");
      var left = el("div");
      left.appendChild(el("span", "name", a.name));
      left.appendChild(el("span", "size", fmtSize(a.size)));
      var link = el("a", "btn small" + (a.name === "lilos.img" ? " primary" : ""), "Télécharger");
      link.setAttribute("href", a.browser_download_url);
      link.setAttribute("rel", "noopener");
      row.appendChild(left);
      row.appendChild(link);
      files.appendChild(row);
    });
    box.appendChild(files);

    var notes = el("p", "t-dim");
    notes.style.margin = "0";
    var more = el("a", null, "Notes de version et empreintes");
    more.setAttribute("href", "https://github.com/" + REPO + "/releases/tag/" + encodeURIComponent(data.tag_name));
    more.setAttribute("rel", "noopener");
    notes.appendChild(more);
    box.appendChild(notes);
  }

  function renderNoRelease(box) {
    box.textContent = "";
    var n = el("div", "notice");
    n.appendChild(el("strong", null, "Pas encore de version publiée. "));
    n.appendChild(document.createTextNode("En attendant, construisez l'image vous-même : "));
    n.appendChild(el("code", null, "git clone https://github.com/" + REPO + " && cd lilos && make"));
    n.appendChild(document.createTextNode(" (voir le README pour les dépendances)."));
    box.appendChild(n);
  }

  var box = document.getElementById("release");
  if (box && window.fetch) {
    fetch("https://api.github.com/repos/" + REPO + "/releases/latest", { headers: { Accept: "application/vnd.github+json" } })
      .then(function (r) { return r.ok ? r.json() : null; })
      .then(function (d) { if (d && d.tag_name) renderRelease(box, d); else renderNoRelease(box); })
      .catch(function () { renderNoRelease(box); });
  }

  // ------------------------------------------------------------------- démo
  var startBtn = document.getElementById("demo-start");
  if (!startBtn) return;
  var resetBtn = document.getElementById("demo-reset");
  var fullBtn = document.getElementById("demo-full");
  var statusEl = document.getElementById("demo-status");
  var placeholder = document.getElementById("demo-placeholder");
  var screen = document.getElementById("screen_container");
  var emulator = null;
  var loading = false;

  function setStatus(t) { statusEl.textContent = t; }

  function loadScript(src) {
    return new Promise(function (resolve, reject) {
      var s = document.createElement("script");
      s.src = src;
      s.onload = resolve;
      s.onerror = function () { reject(new Error("chargement impossible : " + src)); };
      document.head.appendChild(s);
    });
  }

  function boot() {
    emulator = new window.V86({
      wasm_path: "demo/v86.wasm",
      memory_size: 64 * 1024 * 1024,
      vga_memory_size: 2 * 1024 * 1024,
      screen_container: screen,
      bios: { url: "demo/seabios.bin" },
      vga_bios: { url: "demo/vgabios.bin" },
      hda: { url: "demo/lilos.img" },
      disable_speaker: true,          // LilOS n'a pas de son : pas de worklet audio
      autostart: true
    });
    emulator.add_listener("emulator-ready", function () {
      setStatus("en marche");
      resetBtn.disabled = false;
      fullBtn.disabled = false;
      screen.focus();
      if (emulator.keyboard_set_status) emulator.keyboard_set_status(true);
    });
  }

  startBtn.addEventListener("click", function () {
    if (loading || emulator) return;
    loading = true;
    startBtn.disabled = true;
    setStatus("chargement...");
    placeholder.style.display = "none";
    screen.classList.add("on");
    (window.V86 ? Promise.resolve() : loadScript("demo/libv86.js"))
      .then(boot)
      .catch(function (e) {
        setStatus("erreur");
        placeholder.style.display = "";
        screen.classList.remove("on");
        placeholder.textContent = "";
        placeholder.appendChild(el("p", null, "La démo n'a pas pu démarrer (" + e.message + "). Téléchargez l'image pour l'essayer avec QEMU."));
        startBtn.disabled = false;
      })
      .then(function () { loading = false; });
  });

  // Lien direct : https://.../#demarrer lance la démo dès l'arrivée
  if (location.hash === "#demarrer") startBtn.click();

  resetBtn.addEventListener("click", function () {
    if (emulator) { emulator.restart(); setStatus("redémarré"); screen.focus(); }
  });
  fullBtn.addEventListener("click", function () {
    if (emulator && emulator.screen_go_fullscreen) emulator.screen_go_fullscreen();
  });

  // Le clavier n'est capté que lorsque l'écran est actif
  document.addEventListener("click", function (ev) {
    if (!emulator || !emulator.keyboard_set_status) return;
    emulator.keyboard_set_status(screen.contains(ev.target));
  });
})();
