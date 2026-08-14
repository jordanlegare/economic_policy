(() => {
  'use strict';

  const sessionKey = 'cad-policy-studio.session-id';
  const tokenKey = 'cad-policy-studio.api-token';
  const lastRunKey = 'cad-policy-studio.last-run-settings.v1';
  const replaySafeMutationPaths = new Set(['/api/room', '/api/negotiation']);
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

  function storageGet(storage, key) {
    try { return storage?.getItem?.(key) || ''; } catch (_) { return ''; }
  }

  function storageSet(storage, key, value) {
    try { storage?.setItem?.(key, value); return true; } catch (_) { return false; }
  }

  function storageRemove(storage, key) {
    try { storage?.removeItem?.(key); } catch (_) {}
  }

  function clampNumber(value, minimum, maximum) {
    const number = Number(value);
    if (!Number.isFinite(number)) return null;
    return Math.max(minimum, Math.min(maximum, number));
  }

  function sanitizeRunSettings(payload, preserveSavedAt = false) {
    if (!payload || typeof payload !== 'object') return null;
    const savedAt = preserveSavedAt && typeof payload.savedAt === 'string'
      && !Number.isNaN(Date.parse(payload.savedAt))
      ? payload.savedAt
      : new Date().toISOString();
    const settings = {version: 1, savedAt};
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
        const rawValue = Array.isArray(storedCoverage)
          ? storedCoverage[i]
          : payload[`${party}Sector${i}`];
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
      const parsed = JSON.parse(storageGet(globalThis.localStorage, lastRunKey) || 'null');
      return parsed && parsed.version === 1
        ? sanitizeRunSettings(parsed, true)
        : null;
    } catch (_) {
      return null;
    }
  }

  function writeLastRunSettings(payload) {
    const settings = sanitizeRunSettings(payload);
    if (!settings) return false;
    return storageSet(globalThis.localStorage, lastRunKey, JSON.stringify(settings));
  }

  function captureEvaluateRequest(url, init) {
    if (url.pathname !== '/api/evaluate'
        || String(init.method || 'GET').toUpperCase() !== 'POST'
        || typeof init.body !== 'string') return;
    try {
      const payload = JSON.parse(init.body);
      // Comparators, diagnostics and robustness probes are derived work, not an
      // operator run. Only the primary stateful evaluation becomes the restart
      // checkpoint, regardless of when secondary requests are scheduled.
      if (payload?.comparisonOnly === true) return;
      writeLastRunSettings(payload);
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
    if (usPriorityValue && saved.usPriority !== undefined)
      usPriorityValue.textContent = `${saved.usPriority}%`;
    const priorityTotal = globalThis.document.getElementById('priorityTotal');
    if (priorityTotal && saved.canadaPriority !== undefined && saved.usPriority !== undefined) {
      priorityTotal.textContent = `${saved.canadaPriority + saved.usPriority}%`;
    }
    const riskValue = globalThis.document.getElementById('riskAversionValue');
    if (riskValue && saved.riskAversion !== undefined)
      riskValue.textContent = String(saved.riskAversion);
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
    if (partyView && !partyView.hidden && typeof renderPartySectors === 'function')
      renderPartySectors();
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

  // Session identity is not secret and should survive a browser restart so the
  // server-side room/event history can be reopened. Keep the bearer token in
  // sessionStorage; only the non-sensitive session id is durable.
  let sessionId = storageGet(globalThis.localStorage, sessionKey)
    || storageGet(globalThis.sessionStorage, sessionKey);
  if (!sessionId) sessionId = randomId();
  storageSet(globalThis.localStorage, sessionKey, sessionId);
  storageSet(globalThis.sessionStorage, sessionKey, sessionId);

  let mutationSequence = 0;
  function nextMutationOperationId() {
    mutationSequence += 1;
    const entropy = randomId().replace(/[^A-Za-z0-9._:-]/g, '').slice(0, 16);
    return `mut:${sessionId}:${Date.now().toString(36)}:${mutationSequence.toString(36)}:${entropy}`;
  }

  function prepareReplaySafeMutation(url, init) {
    if (!replaySafeMutationPaths.has(url.pathname)
        || String(init.method || 'GET').toUpperCase() !== 'POST'
        || typeof init.body !== 'string') return {init, retryable:false};
    try {
      const payload = JSON.parse(init.body);
      if (!payload || typeof payload !== 'object' || Array.isArray(payload))
        return {init, retryable:false};
      if (typeof payload.operationId !== 'string' || !payload.operationId)
        payload.operationId = nextMutationOperationId();
      return {
        init:{...init, body:JSON.stringify(payload)},
        retryable:true
      };
    } catch (_) {
      return {init, retryable:false};
    }
  }

  function accessToken() {
    if (!globalThis.CAD_API_AUTH_REQUIRED) return '';
    let token = storageGet(globalThis.sessionStorage, tokenKey);
    if (!token) {
      token = globalThis.prompt(
        'This server is listening beyond localhost. Enter its Canada Policy Studio access token.'
      ) || '';
      if (token) storageSet(globalThis.sessionStorage, tokenKey, token);
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

    const prepared = prepareReplaySafeMutation(url, init);
    const requestInit = prepared.init;
    captureEvaluateRequest(url, requestInit);
    const headers = new Headers(
      requestInit.headers
      || (typeof Request !== 'undefined' && input instanceof Request ? input.headers : undefined)
    );
    headers.set('X-CAD-Session-Id', sessionId);
    const token = accessToken();
    if (token) headers.set('Authorization', `Bearer ${token}`);
    const finalInit = {...requestInit, headers};
    const send = () => nativeFetch(input, finalInit);
    if (!prepared.retryable) return send();

    // One replay-safe retry covers a dropped response or transient admission
    // failure. The identical operationId/body is reused, so a first request that
    // committed before the connection failed cannot mutate state twice.
    return send().then(response => {
      if (response && [502, 503, 504].includes(Number(response.status))) return send();
      return response;
    }, () => send());
  };

  if (globalThis.document?.readyState === 'loading') {
    globalThis.document.addEventListener('DOMContentLoaded', armInitialRestore, {once: true});
  } else {
    armInitialRestore();
  }

  globalThis.CAD_SESSION_ID = sessionId;
  globalThis.CADSetApiToken = token => {
    if (token) storageSet(globalThis.sessionStorage, tokenKey, String(token));
    else storageRemove(globalThis.sessionStorage, tokenKey);
  };
  globalThis.CADLastRunSettings = {
    key: lastRunKey,
    read: readLastRunSettings,
    write: writeLastRunSettings,
    restore: restoreLastRunSettings
  };
})();
