# lush extension: function parameters and parameter defaults.
# Single-parameter forms exercise the parser path; multi-parameter
# forms are tracked in #107 (tokenizer glues ',' into the preceding
# WORD so the parameter separator is unreachable).

# Single parameter, no default
greet(name) {
    echo "hello, $name"
}
greet alice

# Single parameter with default value
welcome(greeting=hi) {
    echo "$greeting, world"
}
welcome
welcome hey

# Keyword-style function declaration with parameter
function configure(host=localhost) {
    echo "host=$host"
}
configure
configure example.com
