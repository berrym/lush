# ============================================================================
# Corpus entry (ingested via tests/real_world/_harness)
# ============================================================================
# SOURCE:           https://github.com/ohmyzsh/ohmyzsh/blob/43c6856/lib/correction.zsh
# UPSTREAM-COMMIT:  43c6856
# UPSTREAM-SET:     oh-my-zsh
# LICENSE:          MIT
# BUCKET:           zsh
# ADAPTED:          2026-05-24
# ============================================================================
if [[ "$ENABLE_CORRECTION" == "true" ]]; then
  alias cp='nocorrect cp'
  alias man='nocorrect man'
  alias mkdir='nocorrect mkdir'
  alias mv='nocorrect mv'
  alias sudo='nocorrect sudo'
  alias su='nocorrect su'

  setopt correct_all
fi
