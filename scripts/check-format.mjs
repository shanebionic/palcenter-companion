import { listTextFiles, readText } from "./files.mjs";

const errors = [];

for (const filePath of await listTextFiles()) {
  const content = await readText(filePath);
  const lines = content.split("\n");

  if (content.includes("\r")) {
    errors.push(`${filePath}: use LF line endings`);
  }
  if (!content.endsWith("\n")) {
    errors.push(`${filePath}: add a final newline`);
  }
  lines.forEach((line, index) => {
    if (/[ \t]+$/u.test(line)) {
      errors.push(`${filePath}:${index + 1}: remove trailing whitespace`);
    }
    if (line.includes("\t")) {
      errors.push(`${filePath}:${index + 1}: use spaces instead of tabs`);
    }
  });
}

if (errors.length > 0) {
  console.error(errors.join("\n"));
  process.exitCode = 1;
} else {
  console.log("Formatting check passed.");
}
