// Line icon set (24 px grid, stroke) and the brand mark.
(function () {
  const P = {
    home: '<path d="M3.5 10.5 12 3.8l8.5 6.7"/><path d="M5.5 9.2V20h13V9.2"/><path d="M9.8 20v-5.6h4.4V20"/>',
    pulse: '<path d="M2.5 12h4l2.2-6 4.3 13 2.6-7H21.5"/>',
    list: '<rect x="4" y="3.5" width="16" height="17" rx="3"/><path d="M8 8.5h8M8 12h8M8 15.5h5"/>',
    pin: '<path d="M12 21s-6.5-5.6-6.5-11a6.5 6.5 0 0 1 13 0c0 5.4-6.5 11-6.5 11z"/><circle cx="12" cy="10" r="2.4"/>',
    device: '<rect x="3" y="5" width="18" height="12" rx="3"/><circle cx="12" cy="11" r="3"/><path d="M8 21h8M12 17v4"/>',
    play: '<path d="M8 5.5v13l10.5-6.5z"/>',
    stop: '<rect x="6.5" y="6.5" width="11" height="11" rx="2.5"/>',
    light: '<path d="M9.5 18.5h5M10 21.5h4"/><path d="M12 2.8a6.2 6.2 0 0 0-3.6 11.3c.7.5 1.1 1.3 1.1 2.1v.3h5v-.3c0-.8.4-1.6 1.1-2.1A6.2 6.2 0 0 0 12 2.8z"/>',
    laser: '<circle cx="12" cy="12" r="2.2"/><path d="M12 2.5v5M12 16.5v5M2.5 12h5M16.5 12h5"/>',
    mute: '<path d="M4 9.5h3.2L12 5.5v13l-4.8-4H4z"/><path d="M16 9.5l5 5M21 9.5l-5 5"/>',
    bell: '<path d="M6 16.5V11a6 6 0 0 1 12 0v5.5l1.6 2H4.4z"/><path d="M10 20.8a2.2 2.2 0 0 0 4 0"/>',
    print: '<path d="M7 8.5V3.5h10v5"/><rect x="3.5" y="8.5" width="17" height="8" rx="2.5"/><path d="M7 14h10v6.5H7z"/>',
    chev: '<path d="M9.5 5.5 16 12l-6.5 6.5"/>',
    back: '<path d="M14.5 5.5 8 12l6.5 6.5"/>',
    close: '<path d="M6 6l12 12M18 6 6 18"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    check: '<path d="M5 12.5l4.5 4.5L19 7.5"/>',
    wifi: '<path d="M2.8 9.2a13.5 13.5 0 0 1 18.4 0"/><path d="M5.9 12.6a9 9 0 0 1 12.2 0"/><path d="M9 16a4.4 4.4 0 0 1 6 0"/><circle cx="12" cy="19.2" r="1"/>',
    lan: '<rect x="9" y="3" width="6" height="5" rx="1.2"/><rect x="3" y="16" width="6" height="5" rx="1.2"/><rect x="15" y="16" width="6" height="5" rx="1.2"/><path d="M12 8v4M6 16v-2h12v2"/>',
    search: '<circle cx="11" cy="11" r="6.5"/><path d="m20 20-4.2-4.2"/>',
    refresh: '<path d="M20 11a8 8 0 1 0-2.3 5.7"/><path d="M20 4.5V11h-6.5"/>',
    camera: '<path d="M4 7.5h3l1.8-2.5h6.4L17 7.5h3a1 1 0 0 1 1 1V18a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V8.5a1 1 0 0 1 1-1z"/><circle cx="12" cy="13" r="3.6"/>',
    chip: '<rect x="6" y="6" width="12" height="12" rx="2.5"/><path d="M9 2.5v3.5M15 2.5v3.5M9 18v3.5M15 18v3.5M2.5 9h3.5M2.5 15h3.5M18 9h3.5M18 15h3.5"/>',
    sd: '<path d="M7 3h7.5L18 6.5V20a1 1 0 0 1-1 1H7a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z"/><path d="M9.5 6.5v2.5M12 6.5v2.5M14.5 6.5v2.5"/>',
    nfc: '<path d="M6 8.5a5.5 5.5 0 0 1 0 7"/><path d="M9.5 6a9 9 0 0 1 0 12"/><path d="M13 3.8a12.2 12.2 0 0 1 0 16.4"/>',
    eye: '<path d="M2.5 12S6 5.5 12 5.5 21.5 12 21.5 12 18 18.5 12 18.5 2.5 12 2.5 12z"/><circle cx="12" cy="12" r="3"/>',
    wave: '<path d="M2.5 12c2-5 3.5-5 5.5 0s3.5 5 5.5 0 3.5-5 5.5 0 1.5 2.5 2.5 2.5"/>',
    mic: '<rect x="9" y="3" width="6" height="11" rx="3"/><path d="M5.5 11a6.5 6.5 0 0 0 13 0M12 17.5v3.5"/>',
    bolt: '<path d="M13 2.5 5 13.5h6l-1 8 8-11h-6z"/>',
    temp: '<path d="M10 14.2V5a2 2 0 0 1 4 0v9.2a4 4 0 1 1-4 0z"/><path d="M12 11v5"/>',
    drop: '<path d="M12 3.2S6 9.6 6 14a6 6 0 0 0 12 0c0-4.4-6-10.8-6-10.8z"/>',
    ruler: '<path d="M3 15.5 15.5 3 21 8.5 8.5 21z"/><path d="M7 12l2 2M10 9l2 2M13 6l2 2"/>',
    gauge: '<path d="M4.5 18a8.5 8.5 0 1 1 15 0"/><path d="M12 13.5l4-5"/><circle cx="12" cy="14" r="1.2"/>',
    fan: '<circle cx="12" cy="12" r="1.8"/><path d="M12 10.2c0-4 1-6.7 3.2-6.7 2.4 0 2.6 3.4-.4 5.6M13.8 12c4 0 6.7 1 6.7 3.2 0 2.4-3.4 2.6-5.6-.4M12 13.8c0 4-1 6.7-3.2 6.7-2.4 0-2.6-3.4.4-5.6M10.2 12c-4 0-6.7-1-6.7-3.2 0-2.4 3.4-2.6 5.6.4"/>',
    clock: '<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>',
    power: '<path d="M12 3v8.5"/><path d="M6.6 6.6a7.5 7.5 0 1 0 10.8 0"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"/>',
    moon: '<path d="M19.5 14.5A8 8 0 0 1 9.5 4.5a8 8 0 1 0 10 10z"/>',
    info: '<circle cx="12" cy="12" r="8.5"/><path d="M12 11v5.5M12 7.8v.2"/>',
    shield: '<path d="M12 3 4.5 6v5.5c0 4.6 3.2 8 7.5 9.5 4.3-1.5 7.5-4.9 7.5-9.5V6z"/><path d="m9 12 2.2 2.2L15.5 10"/>',
    share: '<path d="M12 15V3.5M7.5 7.5 12 3l4.5 4.5"/><path d="M5 12v7.5a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1V12"/>',
    download: '<path d="M12 3.5V15M7.5 10.5 12 15l4.5-4.5"/><path d="M4.5 19.5h15"/>',
    sliders: '<path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/>',
    target: '<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="0.8"/>',
    layers: '<path d="M12 3.5 2.5 8.5 12 13.5l9.5-5z"/><path d="m2.5 13 9.5 5 9.5-5"/>',
    trend: '<path d="M3 17.5 9 11.5l4 4 8-8.5"/><path d="M15 7h6v6"/>',
    link: '<path d="M10 14a4 4 0 0 0 5.7 0l3.6-3.6a4 4 0 0 0-5.7-5.7L12.5 5.8"/><path d="M14 10a4 4 0 0 0-5.7 0l-3.6 3.6a4 4 0 0 0 5.7 5.7l1.1-1.1"/>',
    unlink: '<path d="M14.6 6.3 15.5 5.4a4 4 0 0 1 5.7 5.7l-1.6 1.6M9.4 17.7l-.9.9a4 4 0 0 1-5.7-5.7l1.6-1.6"/><path d="M4 4l16 16"/>',
    sparkle: '<path d="M12 3.5 13.8 10.2 20.5 12 13.8 13.8 12 20.5 10.2 13.8 3.5 12 10.2 10.2z"/>',
    edit: '<path d="M4 20h4.5L19.5 9a2.1 2.1 0 0 0-3-3L5.5 17v3z"/>',
    stack: '<rect x="4" y="4" width="16" height="6" rx="2"/><rect x="4" y="14" width="16" height="6" rx="2"/><path d="M8 7h.01M8 17h.01"/>',
    alert: '<path d="M12 4 2.8 19.5h18.4z"/><path d="M12 10v4.5M12 17.2v.2"/>',
    cube: '<path d="M12 2.8 20 7.2v9.6l-8 4.4-8-4.4V7.2z"/><path d="m4 7.2 8 4.4 8-4.4M12 11.6v9.6"/>',
    file: '<path d="M6.5 3h7.5l4.5 4.5V20a1 1 0 0 1-1 1h-11a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z"/><path d="M14 3v4.5h4.5M9 13h6M9 16.5h6"/>',
    grid: '<rect x="4" y="4" width="6.5" height="6.5" rx="1.6"/><rect x="13.5" y="4" width="6.5" height="6.5" rx="1.6"/><rect x="4" y="13.5" width="6.5" height="6.5" rx="1.6"/><rect x="13.5" y="13.5" width="6.5" height="6.5" rx="1.6"/>',
  };
  window.Icon = function (name, cls) {
    return '<svg class="i ' + (cls || '') + '" viewBox="0 0 24 24" aria-hidden="true">' + (P[name] || '') + '</svg>';
  };
  // Brand mark: aperture ring with a wave through it (also drawn by tools/make_icons.py).
  window.Logo = function (size, cls) {
    const id = 'lg' + Math.random().toString(36).slice(2, 7);
    return '<svg class="logo ' + (cls || '') + '" width="' + size + '" height="' + size + '" viewBox="0 0 108 108">' +
      '<defs><linearGradient id="' + id + 'b" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#1b2140"/><stop offset="1" stop-color="#0a0d1a"/></linearGradient>' +
      '<linearGradient id="' + id + 'g" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#5b7cff"/><stop offset="1" stop-color="#a27bff"/></linearGradient></defs>' +
      '<rect width="108" height="108" rx="30" fill="url(#' + id + 'b)"/>' +
      '<circle cx="54" cy="54" r="29" fill="none" stroke="url(#' + id + 'g)" stroke-width="7"/>' +
      '<circle cx="54" cy="54" r="16" fill="none" stroke="url(#' + id + 'g)" stroke-width="2.5" opacity=".45"/>' +
      '<path d="M14 54c6 0 8-14 14-14s8 28 14 28 6-30 12-30 6 30 12 30 8-28 14-28 8 14 14 14" fill="none" stroke="#ffffff" stroke-width="5" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<circle cx="54" cy="54" r="4" fill="#ffffff"/></svg>';
  };
})();
