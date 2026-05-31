# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/ohmyzsh/ohmyzsh/blob/c86ba78e2ff5c5a3e9282a84c0cc220dd3d5f253/lib/prompt_info_functions.zsh
# UPSTREAM-COMMIT:  c86ba78e2ff5c5a3e9282a84c0cc220dd3d5f253
# UPSTREAM-SET:     oh-my-zsh
# LICENSE:          MIT
# BUCKET:           zsh
# ADAPTED:          2026-05-31
# ============================================================================
# *_prompt_info functions for usage in your prompt
#
# Plugin creators, please add your *_prompt_info function to the list
# of dummy implementations to help theme creators not receiving errors
# without the need of implementing conditional clauses.
#
# See also lib/bzr.zsh, lib/git.zsh and lib/nvm.zsh for
# git_prompt_info, bzr_prompt_info and nvm_prompt_info

# Dummy implementations that return false to prevent command_not_found
# errors with themes, that implement these functions
# Real implementations will be used when the respective plugins are loaded
function chruby_prompt_info \
  rbenv_prompt_info \
  hg_prompt_info \
  pyenv_prompt_info \
  svn_prompt_info \
  vi_mode_prompt_info \
  virtualenv_prompt_info \
  jenv_prompt_info \
  azure_prompt_info \
  tf_prompt_info \
  conda_prompt_info \
{
  return 1
}

# oh-my-zsh supports an rvm prompt by default
# get the name of the rvm ruby version
function rvm_prompt_info() {
  [ -f $HOME/.rvm/bin/rvm-prompt ] || return 1
  local rvm_prompt
  rvm_prompt=$($HOME/.rvm/bin/rvm-prompt ${=ZSH_THEME_RVM_PROMPT_OPTIONS} 2>/dev/null)
  [[ -z "${rvm_prompt}" ]] && return 1
  echo "${ZSH_THEME_RUBY_PROMPT_PREFIX}${rvm_prompt:gs/%/%%}${ZSH_THEME_RUBY_PROMPT_SUFFIX}"
}

ZSH_THEME_RVM_PROMPT_OPTIONS="i v g"


# use this to enable users to see their ruby version, no matter which
# version management system they use
function ruby_prompt_info() {
  echo "$(rvm_prompt_info || rbenv_prompt_info || chruby_prompt_info)"
}
