# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/ohmyzsh/ohmyzsh/blob/c86ba78e2ff5c5a3e9282a84c0cc220dd3d5f253/lib/nvm.zsh
# UPSTREAM-COMMIT:  c86ba78e2ff5c5a3e9282a84c0cc220dd3d5f253
# UPSTREAM-SET:     oh-my-zsh
# LICENSE:          MIT
# BUCKET:           zsh
# ADAPTED:          2026-05-31
# ============================================================================
# get the nvm-controlled node.js version
function nvm_prompt_info() {
  which nvm &>/dev/null || return
  local nvm_prompt=${$(nvm current)#v}
  echo "${ZSH_THEME_NVM_PROMPT_PREFIX}${nvm_prompt:gs/%/%%}${ZSH_THEME_NVM_PROMPT_SUFFIX}"
}
