preset_files = [
  'black-on-white.desktop.in',
  'dark-pastels.desktop.in',
  'green-on-black.desktop.in',
  'solarized-dark.desktop.in',
  'solarized-light.desktop.in',
  'tango.desktop.in',
  'white-on-black.desktop.in',
  'xterm.desktop.in',
]

foreach file : preset_files
  i18n.merge_file(
    input: file,
    output: fs.replace_suffix(fs.stem(file), '.theme'),
    po_dir: '..' / 'po',
    type: 'desktop',
    install: true,
    install_dir: get_option('prefix') / get_option('datadir') / 'xfce4' / 'terminal' / 'colorschemes',
  )
endforeach

# presets of their own: color data only, no translation pipeline (the names
# are either brand names of the themes or carry a ready-made localization)
own_presets = [
  'catppuccin-latte.theme',
  'catppuccin.theme',
  'ethereal.theme',
  'everforest.theme',
  'flexoki-light.theme',
  'gruvbox.theme',
  'hackerman.theme',
  'kanagawa.theme',
  'last-horizon.theme',
  'lumon.theme',
  'lupine.theme',
  'matte-black.theme',
  'miasma.theme',
  'nord.theme',
  'orange-green.theme',
  'osaka-jade.theme',
  'retro-82.theme',
  'ristretto.theme',
  'rose-pine.theme',
  'solitude.theme',
  'starship.theme',
  'tokyo-night.theme',
  'vantablack.theme',
  'white.theme',
]

install_data(
  own_presets,
  install_dir: get_option('prefix') / get_option('datadir') / 'xfce4' / 'terminal' / 'colorschemes',
)
