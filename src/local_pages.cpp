#include "local_pages.h"

#include <cstring>

namespace glassglide {

NavigationDecision decide_navigation(const char* uri, bool is_navigation, Page& page)
{
    if (uri && !std::strcmp(uri, "about:blank"))
        return NavigationDecision::AllowLocal;
    if (is_navigation && uri && !std::strncmp(uri, "glass://", 8)
        && resolve_address(uri, page))
        return NavigationDecision::OpenPage;
    return NavigationDecision::Block;
}

std::string local_page(Page page)
{
    std::string html = R"HTML(<!doctype html>
<html lang="it">
<head>
<meta charset="utf-8">
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>GlassGlide - anteprima locale</title>
<style>
  * { box-sizing: border-box; }
  body { margin: 0; min-height: 100vh; padding: clamp(155px, 13vw, 260px) 7% 8%;
    background: linear-gradient(135deg, #071426, #133659); color: #eef6ff;
    font-family: sans-serif; }
  main { max-width: 1500px; margin: auto; }
  .eyebrow { color: #83e6cf; font-size: 1.15rem; font-weight: bold; letter-spacing: .12em; }
  h1 { margin: .5rem 0 1rem; font-size: clamp(2.5rem, 4vw, 5rem); }
  p { color: #cbdced; font-size: clamp(1.25rem, 1.5vw, 2rem); line-height: 1.5; }
  .cards { display: flex; flex-wrap: wrap; gap: 1.25rem; margin-top: 2rem; }
  .card { display: block; flex: 1 1 16rem; padding: 1.6rem; border: 2px solid #416585;
    border-radius: 1.2rem; color: #eef6ff; text-decoration: none; background: #102a43; }
  .card strong { display: block; font-size: 1.7rem; margin-bottom: .5rem; }
  .card span { font-size: 1.15rem; color: #bdd2e5; }
  .card:focus, button:focus { outline: 4px solid #86e8d0; outline-offset: 3px; }
  .note { margin-top: 2.5rem; padding: 1.2rem 1.5rem; border-left: 5px solid #efc06e;
    background: #192f43; font-size: 1.15rem; color: #ffe3a2; }
  button { padding: .8rem 1.4rem; border: 0; border-radius: .7rem;
    background: #85dec9; color: #092134; font-size: 1.3rem; font-weight: bold; }
  .spacer { height: 18rem; }
  #input { color: #9de8d7; }
</style>
</head>
<body><main>)HTML";
    switch (page) {
    case Page::Home:
        html += R"HTML(
<div class="eyebrow">IL TUO BROWSER, IN PREPARAZIONE</div>
<h1>Benvenuto in GlassGlide</h1>
<p>Una piccola area di prova per esplorare schede, navigazione e telecomando.</p>
<div class="cards">
  <a class="card" href="glass://scopri"><strong>Scopri</strong><span>Visita le pagine locali.</span></a>
  <a class="card" href="glass://demo"><strong>Pagina di prova</strong><span>Prova tasti, puntatore e scorrimento.</span></a>
  <a class="card" href="glass://guida"><strong>Guida</strong><span>Impara a usare i controlli.</span></a>
</div>
<div class="note">Anteprima offline: gli indirizzi web sono bloccati fino alla verifica dell'isolamento.</div>)HTML";
        break;
    case Page::Discover:
        html += R"HTML(
<div class="eyebrow">PAGINE LOCALI</div>
<h1>Scopri</h1>
<p>Queste pagine sono incorporate nell'app. Puoi aprirle, tornare indietro e salvarle nei preferiti.</p>
<div class="cards">
  <a class="card" href="glass://demo"><strong>Pagina interattiva</strong><span>Una pagina con JavaScript e scorrimento.</span></a>
  <a class="card" href="glass://guida"><strong>Come funziona</strong><span>Controlli, schede e dati.</span></a>
  <a class="card" href="glass://home"><strong>Home</strong><span>Torna all'inizio.</span></a>
</div>)HTML";
        break;
    case Page::Demo:
        html += R"HTML(
<div class="eyebrow">HTML + CSS + JAVASCRIPT</div>
<h1>Pagina di prova</h1>
<p>Seleziona <strong>Pagina</strong> nella barra per inviare i tasti a questa pagina.
Selezionalo di nuovo con il puntatore per tornare ai controlli del browser.</p>
<p id="input">JavaScript non ancora attivo.</p>
<button id="counter" type="button">Prova il pulsante: 0</button>
<div class="cards">
  <a class="card" href="glass://scopri"><strong>Scopri</strong><span>Apri un'altra pagina.</span></a>
  <a class="card" href="glass://home"><strong>Home</strong><span>Ritorna all'inizio.</span></a>
</div>
<div class="spacer"></div>
<h2>Hai raggiunto il fondo della pagina.</h2>
<p>Usa le frecce su e giu per scorrere; seleziona Pagina per tornare alla barra.</p>)HTML";
        break;
    case Page::Guide:
        html += R"HTML(
<div class="eyebrow">GUIDA RAPIDA</div>
<h1>Comandi e dati</h1>
<p>Usa le frecce per spostare il riquadro evidenziato e OK per selezionare.
La barra indirizzi contiene scorciatoie e una tastiera a schermo.</p>
<p>Puoi aprire fino a sei schede. Solo la pagina attiva resta caricata:
passando a un'altra scheda la pagina viene ricaricata.</p>
<p>Preferiti, cronologia e schede normali sono salvati localmente.
Le schede private non sono salvate e non aggiungono voci alla cronologia.</p>
<div class="cards">
  <a class="card" href="glass://demo"><strong>Prova i comandi</strong><span>Apri la pagina interattiva.</span></a>
  <a class="card" href="glass://home"><strong>Home</strong><span>Torna all'inizio.</span></a>
</div>
<div class="note">La navigazione web e i download sono disabilitati in questa anteprima.</div>)HTML";
        break;
    }
    html += R"HTML(
</main>
<script>
  (function () {
    var status = document.getElementById('input');
    var counter = document.getElementById('counter');
    var count = 0;
    if (status) status.textContent = 'JavaScript attivo. Premi una freccia o seleziona il pulsante.';
    if (counter) counter.addEventListener('click', function () {
      counter.textContent = 'Prova il pulsante: ' + (++count);
    });
    document.addEventListener('keydown', function (event) {
      if (status) status.textContent = 'Tasto ricevuto: ' + event.key;
      if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
        window.scrollBy(0, event.key === 'ArrowDown' ? 140 : -140);
        event.preventDefault();
      } else if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
        var items = Array.prototype.slice.call(document.querySelectorAll('a, button'));
        if (!items.length) return;
        var index = items.indexOf(document.activeElement);
        index = (index + (event.key === 'ArrowRight' ? 1 : -1) + items.length) % items.length;
        items[index].focus();
        event.preventDefault();
      }
    });
  }());
</script>
</body></html>)HTML";
    return html;
}

} // namespace glassglide
