Import("env")

from pathlib import Path

elf = Path(env.subst("$BUILD_DIR")) / "firmware.elf"
template = Path(env.subst("$PROJECT_DIR")) / "renode" / "stm32f103.template.resc"
output = Path(env.subst("$PROJECT_DIR")) / "renode" / "stm32f103.generated.resc"

text = template.read_text()
text = text.replace("@@ELF@@", elf.as_posix())

output.write_text(text)

print(f"Renode ELF: {elf}")