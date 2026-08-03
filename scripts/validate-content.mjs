import { listTextFiles, readText } from "./files.mjs";

const placeholders = [
  /\bTODO\b/u,
  /\bFIXME\b/u,
  /your project name/iu,
  /replace this/iu,
];
const errors = [];

for (const filePath of await listTextFiles()) {
  if (filePath.endsWith("validate-content.mjs")) {
    continue;
  }
  const content = await readText(filePath);
  for (const pattern of placeholders) {
    if (pattern.test(content)) {
      errors.push(`${filePath}: contains placeholder text matching ${pattern}`);
    }
  }
}

if (errors.length > 0) {
  console.error(errors.join("\n"));
  process.exitCode = 1;
} else {
  console.log("Content lint passed.");
}
