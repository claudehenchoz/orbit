// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Deterministic .fwsc validation checks; this test uses package data only.

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import {
  logicalImage, productOf, STOCK_V15_SHA256, STOCK_V15_SIZE, STOCK_V15_PRODUCT,
  validatePackage, validateStockPackage
} from "./fm1pkg.js";

const packagePath = fileURLToPath(new URL("../build/orbit.fwsc", import.meta.url));
const raw = new Uint8Array(readFileSync(packagePath));
const fail = [];
const check = (condition, label) => {
  console.log(`${label.padEnd(64)} ${condition ? "ok" : "FAIL"}`);
  if (!condition) fail.push(label);
};
const rejects = (bytes, label) => {
  let rejected = false;
  try { validatePackage(bytes); } catch { rejected = true; }
  check(rejected, label);
};
const crc16 = (data) => {
  let crc = 0;
  for (const b of data) {
    crc ^= b << 8;
    for (let bit = 0; bit < 8; bit++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
  }
  return crc;
};
const dec = (data) => {
  const out = Uint8Array.from(data);
  let key = 0xFFFF;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 255;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
  return out;
};
const put16 = (data, offset, value) => { data[offset] = value & 255; data[offset + 1] = value >>> 8; };
const put32 = (data, offset, value) => {
  for (let i = 0; i < 4; i++) data[offset + i] = Math.floor(value / 2 ** (8 * i)) & 255;
};
const encode = (data) => dec(data);
const refreshHeaderCrc = (image) => {
  const header = dec(image.subarray(0, 0x40));
  put16(header, 0, crc16(header.subarray(2)));
  image.set(encode(header), 0);
};
const refreshListCrc = (image) => {
  const header = dec(image.subarray(0, 0x40));
  const count = header[8] | header[9] << 8;
  put16(header, 2, crc16(image.subarray(0x40, 0x40 + 0x50 * count)));
  put16(header, 0, crc16(header.subarray(2)));
  image.set(encode(header), 0);
};
const withImage = (edit) => {
  const mutated = Uint8Array.from(raw);
  const image = logicalImage(mutated);
  edit(image);
  /* Reinsert the logical image while retaining each product marker byte. */
  for (let i = 0; i < 20; i++) mutated.set(image.subarray(i * 0x2F, (i + 1) * 0x2F), i * 0x30);
  mutated.set(image.subarray(20 * 0x2F), 20 * 0x30);
  return mutated;
};

const valid = validatePackage(raw);
const cli = execFileSync(process.env.PYTHON || "python3", ["-c",
  "import sys; sys.path.insert(0,'tools'); import fm1_install as i; p,x=i.load_package(sys.argv[1],False); sys.stdout.buffer.write(p.encode()+b'\\0'+x)",
  packagePath], { maxBuffer: 1 << 26 });
const split = cli.indexOf(0);
check(valid.product === cli.subarray(0, split).toString() && Buffer.from(valid.image).equals(cli.subarray(split + 1)),
  `valid package matches CLI product and image (${valid.product})`);
check(valid.product === productOf(raw) && Buffer.from(valid.image).equals(Buffer.from(logicalImage(raw))),
  "valid package retains productOf and logicalImage parity");

rejects(new Uint8Array(100), "short package rejected");
const wrongProduct = Uint8Array.from(raw);
const firstIdentityMarker = Array.from({ length: 20 }, (_, i) => i).find((i) => wrongProduct[i * 0x30 + 0x2F] !== 0x7D);
wrongProduct[firstIdentityMarker * 0x30 + 0x2F] = 0x5F + firstIdentityMarker + 1;
rejects(wrongProduct, "malformed product identity rejected");
const missingLoader = Uint8Array.from(raw);
const loaderAt = Buffer.from(missingLoader).indexOf(Buffer.from("FELUCCA-LOADER-1"));
if (loaderAt >= 0) missingLoader[loaderAt] ^= 1;
rejects(missingLoader, "missing loader marker rejected");
rejects(withImage((image) => { image[0] ^= 1; }), "encrypted header corruption rejected");
rejects(withImage((image) => { image[0x40] ^= 1; }), "file-list corruption rejected");
rejects(withImage((image) => {
  const header = dec(image.subarray(0, 0x40));
  const count = header[8] | header[9] << 8;
  let fileOffset = 0;
  for (let i = 0; i < count; i++) {
    const entry = dec(image.subarray(0x40 + i * 0x50, 0x40 + (i + 1) * 0x50));
    const offset = entry[8] | entry[9] << 8 | entry[10] << 16 | entry[11] << 24;
    const size = entry[12] | entry[13] << 8 | entry[14] << 16 | entry[15] << 24;
    if (size) { fileOffset = offset; break; }
  }
  image[fileOffset] ^= 1;
}), "file-data corruption rejected");
rejects(withImage((image) => {
  const header = dec(image.subarray(0, 0x40));
  const count = header[8] | header[9] << 8;
  const entry = dec(image.subarray(0x40, 0x40 + 0x50));
  put32(entry, 8, image.length - 4);
  put32(entry, 12, 128);
  image.set(encode(entry), 0x40);
  refreshListCrc(image);
  if (count < 1) throw new Error("expected the current package to contain a file");
}), "out-of-bounds file extent rejected");
rejects(withImage((image) => {
  const header = dec(image.subarray(0, 0x40));
  put16(header, 8, 9);
  put16(header, 0, crc16(header.subarray(2)));
  image.set(encode(header), 0);
}), "invalid file count rejected");

check(STOCK_V15_SIZE === 699956 && STOCK_V15_PRODUCT === "FM-1_015"
  && STOCK_V15_SHA256 === "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
  && typeof validateStockPackage === "function", "stock V15 exact identity, size and hash pins remain unchanged");
const stock = new Uint8Array(STOCK_V15_SIZE);
let stockRejects = false;
try { await validateStockPackage(stock); } catch { stockRejects = true; }
check(stockRejects, "stock V15 exact size and SHA-256 pin remains enforced");

if (fail.length) {
  console.error(`PACKAGE TESTS FAILED (${fail.length})`);
  process.exitCode = 1;
} else {
  console.log("package tests passed");
}
