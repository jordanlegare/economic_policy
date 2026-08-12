(() => {
  'use strict';

  const root = document.getElementById('dashboardView');
  if (!root) return;

  const panels = [...root.querySelectorAll('details.dashboard-panel[data-dashboard-panel]')];
  if (!panels.length) return;

  const storageKey = 'economic-policy-dashboard-panels-v1';
  const readState = () => {
    try {
      const parsed = JSON.parse(localStorage.getItem(storageKey) || '{}');
      return parsed && typeof parsed === 'object' ? parsed : {};
    } catch (_) {
      return {};
    }
  };
  const writeState = () => {
    try {
      const state = {};
      panels.forEach(panel => { state[panel.dataset.dashboardPanel] = panel.open; });
      localStorage.setItem(storageKey, JSON.stringify(state));
    } catch (_) {
      // Panel behavior remains fully functional when storage is unavailable.
    }
  };

  const saved = readState();
  panels.forEach(panel => {
    const id = panel.dataset.dashboardPanel;
    if (Object.prototype.hasOwnProperty.call(saved, id)) panel.open = saved[id] === true;
    panel.addEventListener('toggle', writeState);
  });

  const setAll = open => {
    panels.forEach(panel => { panel.open = open; });
    writeState();
  };

  const collapse = document.getElementById('collapseDashboardPanels');
  const expand = document.getElementById('expandDashboardPanels');
  if (collapse) collapse.addEventListener('click', () => setAll(false));
  if (expand) expand.addEventListener('click', () => setAll(true));

  window.DashboardPanels = {
    setAll,
    panels: () => panels.map(panel => ({
      id: panel.dataset.dashboardPanel,
      open: panel.open
    }))
  };
})();