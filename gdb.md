# resources
https://periwinkle.sh/blog/nxgdb/
https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf
https://gist.github.com/jam1garner/c9ba6c0cff150f1a2480d0c18ff05e33

# commands
set print pretty on
set print array on
set print demangle on
target extended-remote 192.168.100.220:22225
monitor wait application
attach <PID>
continue
monitor get info
symbol-file swirrent.elf -o <offset>
