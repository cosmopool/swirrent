local dap = require("dap")

dap.adapters.gdb = {
  id = "gdb",
  type = "executable",
  command = "gdb",
  args = { "--quiet", "--interpreter=dap" },
}

dap.configurations.c = {
  {
    name = "Debug decoder",
    type = "lldb",
    request = "launch",
    program = function()
      -- os.execute("clang -g -O0 -c -o decoder.o main.c && clang -g -O0 -o decoder decoder.o")
      os.execute("clang -g -O0 -o decoder main.c")
      return "${workspaceFolder}/decoder"
    end,
    cwd = "${workspaceFolder}",
    stopOnEntry = false,
    -- args = function()
    --   local file = vim.fn.input("Path to binary file to decode: ", vim.fn.getcwd() .. "/", "file")
    --   return { file }
    -- end,
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
