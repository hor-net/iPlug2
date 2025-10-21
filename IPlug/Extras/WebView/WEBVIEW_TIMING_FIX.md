# WebView JavaScript Timing Fix

## Problema Risolto

Il problema originale era che le funzioni JavaScript (come `SAMFD`, `SCVFD`, `SCMFD`, ecc.) venivano inviate alla WebView prima che questa fosse completamente caricata e pronta a ricevere i messaggi. Questo causava la perdita di messaggi importanti, specialmente durante l'inizializzazione del plugin.

## Soluzione Implementata

È stato implementato un sistema di coda per i messaggi JavaScript che garantisce che tutti i messaggi vengano consegnati correttamente, anche se inviati prima che la WebView sia pronta.

### Modifiche Apportate

#### 1. IPlugWebViewEditorDelegate.h

**Nuove variabili membro aggiunte:**
```cpp
#include <queue>
#include <mutex>
#include <string>

private:
  std::queue<std::string> mJavaScriptQueue;
  bool mWebViewReady = false;
  std::mutex mQueueMutex;
```

**Nuove dichiarazioni di funzioni:**
```cpp
void QueueJavaScript(const char* scriptStr);
void FlushJavaScriptQueue();
```

**Funzioni modificate per usare la coda:**
- `SendControlValueFromDelegate()` - ora usa `QueueJavaScript()`
- `SendControlMsgFromDelegate()` - ora usa `QueueJavaScript()`
- `SendParameterValueFromDelegate()` - ora usa `QueueJavaScript()`
- `SendArbitraryMsgFromDelegate()` - ora usa `QueueJavaScript()`
- `SendMidiMsgFromDelegate()` - ora usa `QueueJavaScript()`
- `SendJSONFromDelegate()` - ora usa `QueueJavaScript()`

**OnWebContentLoaded() modificata:**
- Invia i parametri iniziali direttamente usando `IWebView::EvaluateJavaScript()`
- Chiama `FlushJavaScriptQueue()` per processare i messaggi in coda

#### 2. IPlugWebViewEditorDelegate.mm

**Nuove funzioni implementate:**

```cpp
void QueueJavaScript(const char* scriptStr)
{
  std::lock_guard<std::mutex> lock(mQueueMutex);
  
  if (mWebViewReady) {
    // WebView is ready, execute immediately
    IWebView::EvaluateJavaScript(scriptStr);
  } else {
    // WebView not ready, add to queue
    mJavaScriptQueue.push(std::string(scriptStr));
  }
}

void FlushJavaScriptQueue()
{
  std::lock_guard<std::mutex> lock(mQueueMutex);
  
  while (!mJavaScriptQueue.empty()) {
    const std::string& script = mJavaScriptQueue.front();
    IWebView::EvaluateJavaScript(script.c_str());
    mJavaScriptQueue.pop();
  }
  
  mWebViewReady = true;
}
```

**Costruttore modificato:**
- Inizializza `mWebViewReady = false`

## Come Funziona

1. **Stato Iniziale**: `mWebViewReady` è impostato a `false` nel costruttore
2. **Messaggi Prima del Caricamento**: Tutti i messaggi JavaScript vengono messi in coda invece di essere inviati direttamente
3. **WebView Caricata**: Quando `OnWebContentLoaded()` viene chiamata:
   - I parametri iniziali vengono inviati direttamente
   - `FlushJavaScriptQueue()` viene chiamata per processare tutti i messaggi in coda
   - `mWebViewReady` viene impostato a `true`
4. **Messaggi Successivi**: Tutti i nuovi messaggi vengono inviati immediatamente

## Vantaggi

- **Thread-Safe**: Usa `std::mutex` per proteggere l'accesso alla coda
- **Nessuna Perdita di Messaggi**: Tutti i messaggi vengono garantiti di essere consegnati
- **Performance**: I messaggi vengono inviati immediatamente una volta che la WebView è pronta
- **Compatibilità**: Non cambia l'API pubblica, solo il comportamento interno

## Test

Il sistema è stato testato con un mock che simula il comportamento reale:
- Messaggi inviati prima del caricamento vengono messi in coda
- `OnWebContentLoaded()` processa correttamente la coda
- Messaggi successivi vengono inviati immediatamente

Questo fix risolve definitivamente il problema del timing tra l'invio di messaggi JavaScript e la disponibilità della WebView.