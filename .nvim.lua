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

local function default_config(name, args)
  return {
    name = name,
    type = "lldb",
    request = "launch",
    program = function()
      local ok, _, code = os.execute("./build.sh")
      if ok then return "${workspaceFolder}/swirrent" end
      error("Build failed with: ${code}");
    end,
    cwd = "${workspaceFolder}",
    stopOnEntry = false,
    args = args,
    externalConsole = false,
    runInTerminal = false,
    MIMode = 'gdb',
    setupCommands = {
      {
        text = '-enable-pretty-printing',
        description = 'Enable pretty printing',
        ignoreFailures = false,
      },
    },
  }
end

local function torrent_file()
  for line in io.lines(vim.fn.getcwd() .. "/torrents") do
    line = vim.trim(line)
    if line ~= "" and not line:match("^#") then return line end
  end
  error("No torrent path found in torrents file")
end

local function peers_file()
  for line in io.lines(vim.fn.getcwd() .. "/peers") do
    line = vim.trim(line)
    if line ~= "" and not line:match("^#") then return line end
  end
  error("No peer found in peers file")
end


dap.configurations.c = {
  default_config("Debug", { torrent_file, "-v" }),
  default_config("Debug handshake", { "e.torrent", "-v", "--handshake", peers_file }),
  default_config("Debug load response", { "e.torrent", "-v", "--load-response", "resp.bin" }),
  default_config("Debug dump response", { "e.torrent", "-v", "--dump-response", "resp.bin" }),
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
}
