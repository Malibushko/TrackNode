Import("env")

elf = env.subst("$BUILD_DIR/firmware.elf")

env.Append(
    CPPDEFINES=[
        ("RENODE_ELF", f'"{elf}"')
    ]
)