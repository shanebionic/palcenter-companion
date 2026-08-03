import { access, readFile } from "node:fs/promises";

const requiredPaths = [
  "README.md",
  "LICENSE",
  "CONTRIBUTING.md",
  "CODE_OF_CONDUCT.md",
  "SECURITY.md",
  "CHANGELOG.md",
  "ROADMAP.md",
  "CMakeLists.txt",
  "config/PalCenterCompanion.ini",
  "extension/dllmain.cpp",
  "include/palcenter_companion/application.hpp",
  "src/application.cpp",
  "src/http_server.cpp",
  "tests/companion_tests.cpp",
  "scripts/build-production.ps1",
  "scripts/package-production.ps1",
  "packaging/README.txt",
  "packaging/LICENSES/THIRD-PARTY-NOTICES.txt",
  "docs/ARCHITECTURE.md",
  "docs/INSTALLATION.md",
  "docs/TOOLCHAIN.md",
  "docs/LIVE-PALSERVER-UAT.md",
  "docs/validation/PALSERVER-24466863.md",
  "docs/research/FRAMEWORK-EVALUATION.md",
  "docs/API.md",
  "docs/CAPABILITIES.md",
  "docs/EVENT-MODEL.md",
  "docs/REAL-TIME.md",
  "docs/COORDINATE-SPACES.md",
  "api/openapi.yaml",
  "examples/capabilities.json",
  ".github/workflows/validation.yml",
  ".github/PULL_REQUEST_TEMPLATE.md",
];
const errors = [];

for (const requiredPath of requiredPaths) {
  try {
    await access(requiredPath);
  } catch {
    errors.push(`Missing required path: ${requiredPath}`);
  }
}

const packageMetadata = JSON.parse(await readFile("package.json", "utf8"));
if (packageMetadata.name !== "palcenter-companion") {
  errors.push("package.json must identify palcenter-companion");
}
if (packageMetadata.version !== "0.1.0") {
  errors.push("package.json must begin at version 0.1.0");
}

const apiContract = await readFile("api/openapi.yaml", "utf8");
for (const requiredText of [
  "url: /palcenter/v1",
  "  /health:",
  "  /version:",
  "  /capabilities:",
]) {
  if (!apiContract.includes(requiredText)) {
    errors.push(`OpenAPI contract is missing: ${requiredText.trim()}`);
  }
}

const cmakeProject = await readFile("CMakeLists.txt", "utf8");
for (const requiredText of [
  "project(PalCenterCompanion VERSION 0.1.0",
  "PALCENTER_UE4SS_BUILD_MODE",
  "OUTPUT_NAME \"main\"",
  "palcenter_companion_core",
]) {
  if (!cmakeProject.includes(requiredText)) {
    errors.push(`CMake project is missing: ${requiredText}`);
  }
}

const capabilities = JSON.parse(await readFile("examples/capabilities.json", "utf8"));
for (const name of ["events", "guilds", "bases", "performance", "moderation"]) {
  if (capabilities[name] !== false) {
    errors.push(`Foundation capability ${name} must be false`);
  }
}

if (errors.length > 0) {
  console.error(errors.join("\n"));
  process.exitCode = 1;
} else {
  console.log("Repository structure and contracts validated.");
}
