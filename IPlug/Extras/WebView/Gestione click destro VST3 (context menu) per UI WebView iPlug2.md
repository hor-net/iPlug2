## Specifiche VST3 (click destro “standard”)
- In VST3 il comportamento “standard” del click destro su un controllo legato a un parametro è mostrare il **context menu del host** (es. automazione, MIDI learn, ecc.). Il plug-in deve **chiederlo al host** tramite l’interfaccia opzionale `Vst::IComponentHandler3` e la funzione `createContextMenu(view, paramID)`; il host può pre-compilare il menu in base al `paramID` o creare un menu “generico” se `paramID` è nullo. Fonte: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponentHandler3.html
- Il menu risultante è un `Vst::IContextMenu`: il plug-in può aggiungere voci (opzionale) e poi chiamare `popup(x, y)` per mostrarlo. Le coordinate `x,y` devono essere **relative all’angolo in alto a sinistra della view del plug-in**. Fonte: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IContextMenu.html

## Analisi del repo JS (vincoli reali)
- Questo repo contiene solo il lato JS/HTML: l’unico canale standard verso il nativo è la funzione globale `IPlugSendMsg(messageObj)` (iniettata dal WebView). Le chiamate UI→host esistenti sono in `iplug2.js` (es. `SPVFUI`, `SAMFUI`, `BPCFUI/EPCFUI`).
- Ogni controllo registra una reference su DOM: `this._domElement._iControlInstance = this` nel costruttore di `iControl` in `icontrols.js`, quindi possiamo risalire dal `event.target` al controllo.

## Implementazione proposta (JS)
- Aggiungere in `iplug2.js` una nuova chiamata UI→host, ad es. `CTXMFUI(paramIdx, x, y, dpr, ctrlTag)` che fa `IPlugSendMsg({ msg: "CTXMFUI", ... })`.
- Registrare un listener unico su `document` per l’evento DOM `contextmenu`:
  - `preventDefault()` per sopprimere il menu del browser del WebView.
  - Risalire dal `event.target` all’antenato che espone `_iControlInstance` (o che abbia `data-paramid`) per ottenere `paramIdx`.
  - Calcolare `x,y` in coordinate view: `clientX/clientY` → sottrarre `getBoundingClientRect()` della root (tipicamente `document.documentElement`) e includere `window.devicePixelRatio`.
  - Se non c’è parametro sotto il cursore, inviare `paramIdx = -1` (menu generico) oppure non inviare nulla (decideremo in base al comportamento desiderato; default: menu generico + soppressione menu browser).
- Aggiungere un escape hatch per casi speciali (opzionale): attributo `data-vst3-contextmenu="0"` per non intercettare alcuni elementi.

## Implementazione necessaria (C++ iPlug2 / VST3)
- Nel progetto plugin (non in questo repo), gestire il nuovo messaggio `CTXMFUI` proveniente dal WebView.
- Recuperare `IComponentHandler3` da `componentHandler` dell’EditController e chiamare:
  - `menu = handler3->createContextMenu(view, &paramID)` se `paramIdx>=0` (mappato a `ParamID`), altrimenti `createContextMenu(view, nullptr)`.
  - `menu->popup(x, y)` usando coordinate coerenti con la view (applicare scaling usando `dpr` se necessario).
  - `menu->release()`.

## Verifica
- In un host che supporta il context menu VST3 (es. Cubase) verificare:
  - click destro su knob/fader/switch legato a parametro → compare menu con voci host (automation/MIDI learn ecc.).
  - click destro su area vuota → menu generico o nessun menu (in base a host).
  - comportamento su HiDPI: menu appare vicino al cursore (nessun offset).

## Output atteso
- Niente menu “browser” del WebView.
- Click destro coerente con VST3, demandato al host via `IComponentHandler3::createContextMenu`/`IContextMenu::popup`.
