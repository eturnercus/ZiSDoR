// Сценарии сквозного теста. Внедряется в страницу лаунчера только в сборке с -DGDZ_E2E_HARNESS=ON.
// Сценарий выбирается переменной окружения E2E_SCENARIO, результат печатается строкой "E2E: {json}".
(function () {
  var events = [];

  function wait(pred, ms) {
    return new Promise(function (res, rej) {
      var t0 = Date.now();
      (function poll() {
        if (pred()) return res();
        if (Date.now() - t0 > ms) return rej(new Error('timeout'));
        setTimeout(poll, 100);
      })();
    });
  }
  function has(type) { return function () { return events.some(function (e) { return e.type === type; }); }; }
  function finished() { return events.some(function (e) { return e.type === 'exited' || e.type === 'error' || e.type === 'cancelled'; }); }
  function brief() { return events.filter(function (e) { return e.type !== 'progress'; }); }

  async function run() {
    var out = {};
    try {
      var env = await window.e2e_env();
      out.scenario = env;

      if (env === 'after-update') {
        out.info = await window.gdz_info();
      } else if (env === 'update') {
        out.check = await window.gdz_check_update();
        await window.e2e_mark();
        window.e2e_print(JSON.stringify(out));
        out.apply = await window.gdz_apply_update();
        if (!out.apply.ok) window.e2e_report(JSON.stringify(out));
        return; // при успехе лаунчер сам завершится и перезапустится
      } else if (env === 'cancel') {
        out.play = await window.gdz_play(JSON.stringify({ nick: 'Tester_1', ram: 3 }));
        await wait(function () { return events.some(function (e) { return e.type === 'stage' && e.text === 'Загрузка Java 8'; }); }, 60000);
        await window.gdz_cancel();
        await wait(finished, 60000);
        out.events = brief();
      } else if (env === 'badnick') {
        out.play = await window.gdz_play(JSON.stringify({ nick: 'x y', ram: 3 }));
        await wait(has('error'), 20000);
        out.events = brief();
      } else {
        out.info = await window.gdz_info();
        out.play = await window.gdz_play(JSON.stringify({ nick: 'Tester_1', ram: 3, java: '', gameDir: '', closeOnLaunch: false }));
        await wait(finished, 300000);
        out.events = brief();
        out.progressEvents = events.length - out.events.length;
        out.check = await window.gdz_check_update();
      }
    } catch (e) {
      out.exception = String(e);
      out.events = brief();
    }
    window.e2e_report(JSON.stringify(out));
  }

  window.addEventListener('load', function () {
    var original = window.__gdz;
    window.__gdz = function (ev) { events.push(ev); if (original) original(ev); };
    setTimeout(run, 400);
  });
})();
