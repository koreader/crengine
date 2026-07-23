def component:
  . | sub("^" + $srcdir + "/(?<dir>[^/ ]+/[^/ ]+)/.*$"; "/\(.dir)/") |
  if . == "/crengine/include/" then "/crengine/src/" else . end
;

def escape:
  . | "'" + gsub("'"; "'\\''") + "'"
;

{
  components: [.[] | .match = (.file | component) | .command = (.command | gsub(" -[co] [^ ]+"; "") + " -fsyntax-only")] | unique_by(.match) | [.[] | {key: .match, value: {command: .command, directory: .directory}}] | from_entries,
  files: [$ARGS.positional | unique[] | { component: component, file: . }]
} as $o | $o.files | map([$o.components[.component] // error("component not found: " + .file) | ["env", "--chdir", (.directory | escape), .command]] + [.file | escape] | flatten | join(" ")) | .[]

# vim: sw=2
