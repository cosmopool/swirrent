local dap = require("dap")

dap.adapters.gdb = {
  id = "gdb",
  type = "executable",
  command = "gdb",
  args = { "--quiet", "--interpreter=dap" },
}

-- Custom-built GDB 17.1: aarch64-none-elf target + Python/DAP support
local gdb_switch_cmd = os.getenv("HOME") .. "/.local/bin/aarch64-none-elf-gdb"

dap.adapters.gdb_switch = {
  id = "gdb_switch",
  type = "executable",
  command = gdb_switch_cmd,
  args = { "--quiet", "--interpreter=dap", "--nx" },
}

-- Function adapter: injects config.setup_commands as -ex args BEFORE the DAP
-- loop starts. This lets us use "target extended-remote" (GDB's native DAP
-- only supports "target remote" via the attach target parameter).
dap.adapters.gdb_switch_hw = function(callback, config)
  local args = { "--quiet", "--interpreter=dap", "--nx" }
  for _, cmd in ipairs(config.setup_commands or {}) do
    table.insert(args, "-ex")
    table.insert(args, cmd)
  end
  callback({
    type = "executable",
    command = gdb_switch_cmd,
    args = args,
  })
end

dap.configurations.c = {
  {
    name = "Nintendo Switch (emulator)",
    type = "gdb_switch",
    request = "attach",
    program = vim.fn.getcwd() .. "/swirrent.elf",
    target = "localhost:5555",
    cwd = vim.fn.getcwd(),
    stopOnEntry = true,
  },
  {
    name = "Nintendo Switch (hardware)",
    type = "gdb_switch_hw",
    request = "attach",
    target = "192.168.100.220:22225",
    cwd = vim.fn.getcwd(),
    setup_commands = {
      "target extended-remote 192.168.100.220:22225",
    },
  },
  {
    name = "Debug decoder",
    type = "lldb",
    request = "launch",
    program = "${workspaceFolder}/decoder",
    cwd = "${workspaceFolder}",
    stopOnEntry = false,
    args = function()
      -- local file = vim.fn.input("Path to torrent file to decode: ", vim.fn.getcwd() .. "/", "file")
      -- return { file, "--dump-response", "r.bin" }
      return { "e.torrent", "-v" }
      -- return { "e.torrent", "-v", "--load-response", "resp.bin" }
      -- return { "e.torrent", "-v", "--dump-response", "resp.bin" }
    end,
    externalConsole = false, -- Critical for seeing output in nvim
    runInTerminal = false,
    MIMode = 'gdb',
    setupCommands = {
      {
        text = '-enable-pretty-printing',
        description = 'Enable pretty printing',
        ignoreFailures = false,
      },
    },
  },
}
