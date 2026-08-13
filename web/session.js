(() => {
  'use strict';

  const sessionKey = 'cad-policy-studio.session-id';
  const tokenKey = 'cad-policy-studio.api-token';
  const lastRunKey = 'cad-policy-studio.last-run-settings.v1';
  const scalarRunFields = {
    usTariff: [0, 60],
    retaliatoryTariff: [0, 60],
    canadaPriority: [0, 100],
    usPriority: [0, 100],
    riskAversion: [0, 100],
    cooperationCeiling: [0, 100]
  };

  function randomId() {
    if (globalThis.crypto?.randomUUID) return globalThis.crypto.randomUUID();
    if (globalThis.crypto?.getRandomValues) {
      const bytes = new Uint8Array(16);
      globalThis.crypto.getRandomValues(bytes);
      return Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
    }
    return `session-${Date.now()}-${Math.random().toString(36).slice(2)}`;
  }

  function clampNumber(value, minimum, maximum) {
    const number = Number(value);
    if (!Number.isFinite(number)) return null;
    return Math.max(minimum, Math.min(maximum, number));
  }

  function sanitizeRunSettings(payload) {
    if (!payload || typeof payload !== 'object') return null;
    const settings = {version: 1, savedAt: new Date().toISOString()};
    let populated = false;

    Object.entries(scalarRunFields).forEach(([field, bounds]) => {
      const value = clampNumber(payload[field], bounds[0], bounds[1]);
      if (value !== null) {
        settings[field] = value;
        populated = true;
      }
    });

    ['us', 'canada'].forEach(party => {
      const storedCoverage = payload[`${party}SectorCoverage`];
      const coverage = [];
      for (let i = 0; i < 20; i++) {
        const rawValue = Array.isArray(storedCoverage) ? storedCoverage[i] : payload[`${party}Sector${i}`];
        const value = clampNumber(rawValue, 0, 100);
        if (value === null) return;
        coverage.push(value);
      }
      if (coverage.length === 20) {
        settings[`${party}SectorCoverage`] = coverage;
        populated = true;
      }
    });

    return populated ? settings : null;
  }

  function readLastRunSettings() {
    try {
      const parsed = JSON.parse(localStorage.getItem(lastRunKey) || 'null');
      return parsed && parsed.version === 1 ? sanitizeRunSettings(parsed) : null;
    } catch (_) {
      return null;
    }
  }

  function writeLastRunSettings(payload) {
    const settings = sanitizeRunSettings(payload);
    if (!settings) return;
    try {
      localStorage.setItem(lastRunKey, JSON.stringify(settings));
    } catch (_) {}
  }

  function captureEvaluateRequest(url, init) {
    if (url.pathname !== '/api/evaluate' || String(init.method || 'GET').toUpperCase() !== 'POST') return;
    if (typeof init.body !== 'string') return;
    try {
      writeLastRunSettings(JSON.parse(init.body));
    } catch (_) {}
  }

  function setControlValue(id, value) {
    const control = globalThis.document?.getElementById?.(id);
    if (control && value !== undefined) control.value = String(value);
    return control;
  }

  function restoreLastRunSettings(saved = readLastRunSettings()) {
    if (!saved || !globalThis.document) return false;

    const opening = globalThis.InitialOpeningScenario?.opening;
    if (opening) {
      if (saved.usTariff !== undefined) opening.usTariff = saved.usTariff;
      if (saved.retaliatoryTariff !== undefined) opening.retaliatoryTariff = saved.retaliatoryTariff;
      if (saved.usSectorCoverage?.length === 20) {
        opening.usSectorCoverage.splice(0, opening.usSectorCoverage.length, ...saved.usSectorCoverage);
      }
      if (saved.canadaSectorCoverage?.length === 20) {
        opening.canadaSectorCoverage.splice(0, opening.canadaSectorCoverage.length, ...saved.canadaSectorCoverage);
      }
    }

    setControlValue('usTariff', saved.usTariff);
    setControlValue('retaliatoryTariff', saved.retaliatoryTariff);
    setControlValue('canadaPriority', saved.canadaPriority);
    setControlValue('usPriority', saved.usPriority);
    setControlValue('riskAversion', saved.riskAversion);
    setControlValue('cooperationCeiling', saved.cooperationCeiling);

    if (typeof positions !== 'undefined') {
      if (saved.usSectorCoverage?.length === 20) {
        positions.us.splice(0, positions.us.length, ...saved.usSectorCoverage);
      }
      if (saved.canadaSectorCoverage?.length === 20) {
        positions.canada.splice(0, positions.canada.length, ...saved.canadaSectorCoverage);
      }
    }

    const canadaPriorityValue = globalThis.document.getElementById('canadaPriorityValue');
    if (canadaPriorityValue && saved.canadaPriority !== undefined) {
      canadaPriorityValue.textContent = `${saved.canadaPriority}%`;
    }
    const usPriorityValue = globalThis.document.getElementById('usPriorityValue');
    if (usPriorityValue && saved.usPriority !== undefined) usPriorityValue.textContent = `${saved.usPriority}%`;
    const priorityTotal = globalThis.document.getElementById('priorityTotal');
    if (priorityTotal && saved.canadaPriority !== undefined && saved.usPriority !== undefined) {
      priorityTotal.textContent = `${saved.canadaPriority + saved.usPriority}%`;
    }
    const riskValue = globalThis.document.getElementById('riskAversionValue');
    if (riskValue && saved.riskAversion !== undefined) riskValue.textContent = String(saved.riskAversion);
    const cooperationValue = globalThis.document.getElementById('cooperationCeilingValue');
    if (cooperationValue && saved.cooperationCeiling !== undefined) {
      cooperationValue.textContent = `${saved.cooperationCeiling}%`;
    }
    const retaliationValue = globalThis.document.getElementById('retaliatoryTariffValue');
    if (retaliationValue && saved.retaliatoryTariff !== undefined) {
      retaliationValue.textContent = `${saved.retaliatoryTariff}%`;
    }

    if (typeof updateTariff === 'function') updateTariff();
    if (typeof updatePosition === 'function') updatePosition();
    if (typeof syncPartyView === 'function') syncPartyView();
    if (typeof refreshPartySectorMetrics === 'function') refreshPartySectorMetrics();
    const partyView = globalThis.document.getElementById('partyView');
    if (partyView && !partyView.hidden && typeof renderPartySectors === 'function') renderPartySectors();
    return true;
  }

  function armInitialRestore() {
    const saved = readLastRunSettings();
    if (!saved) return;

    restoreLastRunSettings(saved);
    const currentEvaluate = globalThis.evaluate;
    if (typeof currentEvaluate !== 'function' || currentEvaluate.__cadRestoresLastRun) return;

    let pending = true;
    const wrappedEvaluate = async function(...args) {
      if (pending) {
        pending = false;
        restoreLastRunSettings(saved);
      }
      return currentEvaluate.apply(this, args);
    };
    wrappedEvaluate.__cadRestoresLastRun = true;
    globalThis.evaluate = wrappedEvaluate;
  }

  let sessionId = sessionStorage.getItem(sessionKey);
  if (!sessionId) {
    sessionId = randomId();
    sessionStorage.setItem(sessionKey, sessionId);
  }

  function accessToken() {
    if (!globalThis.CAD_API_AUTH_REQUIRED) return '';
    let token = sessionStorage.getItem(tokenKey) || '';
    if (!token) {
      token = globalThis.prompt(
        'This server is listening beyond localhost. Enter its Canada Policy Studio access token.'
      ) || '';
      if (token) sessionStorage.setItem(tokenKey, token);
    }
    return token;
  }

  const nativeFetch = globalThis.fetch.bind(globalThis);
  globalThis.fetch = (input, init = {}) => {
    const rawUrl = typeof input === 'string' || input instanceof URL ? input : input.url;
    const url = new URL(rawUrl, globalThis.location.href);
    if (url.origin !== globalThis.location.origin || !url.pathname.startsWith('/api/')) {
      return nativeFetch(input, init);
    }

    captureEvaluateRequest(url, init);
    const headers = new Headers(
      init.headers || (typeof Request !== 'undefined' && input instanceof Request ? input.headers : undefined)
    );
    headers.set('X-CAD-Session-Id', sessionId);
    const token = accessToken();
    if (token) headers.set('Authorization', `Bearer ${token}`);
    return nativeFetch(input, {...init, headers});
  };

  if (globalThis.document?.readyState === 'loading') {
    globalThis.document.addEventListener('DOMContentLoaded', armInitialRestore, {once: true});
  } else {
    armInitialRestore();
  }

  globalThis.CAD_SESSION_ID = sessionId;
  globalThis.CADSetApiToken = token => {
    if (token) sessionStorage.setItem(tokenKey, String(token));
    else sessionStorage.removeItem(tokenKey);
  };
  globalThis.CADLastRunSettings = {
    key: lastRunKey,
    read: readLastRunSettings,
    restore: restoreLastRunSettings
  };
})();
