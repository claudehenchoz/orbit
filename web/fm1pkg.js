// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// FM-1 .fwsc packages in the browser: the package identity and the image the
// device reads during an update.

const BLOCKS = 20, BLK = 0x30, KEEP = 0x2F;
const LOADER_MARK = new TextEncoder().encode("FELUCCA-LOADER-1");

// the package identity ("FM-1_904"): one marker byte after each of the first 20 blocks
export function productOf(fwsc) {
  if (fwsc.length < BLOCKS * BLK) throw new Error("not an FM-1 package (too short)");
  let s = "";
  for (let i = 0; i < BLOCKS; i++) {
    const m = fwsc[i * BLK + KEEP];
    if (m !== 0x7D) s += String.fromCharCode((m - i - 1) & 0xFF);
  }
  return s;
}

// the image the device addresses during an update: the .fwsc without the 20 marker bytes
export function logicalImage(fwsc) {
  const out = new Uint8Array(fwsc.length - BLOCKS);
  for (let i = 0; i < BLOCKS; i++) out.set(fwsc.subarray(i * BLK, i * BLK + KEEP), i * KEEP);
  out.set(fwsc.subarray(BLOCKS * BLK), BLOCKS * KEEP);
  return out;
}

function crc16(data) {
  let crc = 0;
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
  }
  return crc;
}

function decode(data) {
  const out = Uint8Array.from(data);
  let key = 0xFFFF;
  for (let i = 0; i < out.length; i++) {
    out[i] ^= key & 0xFF;
    key = ((key << 1) ^ (key & 0x8000 ? 0x1021 : 0)) & 0xFFFF;
  }
  return out;
}

function u16(data, offset) {
  return data[offset] | data[offset + 1] << 8;
}

function u32(data, offset) {
  return (data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 | data[offset + 3] << 24) >>> 0;
}

function packageDamage(image) {
  if (image.length < 0x40) return "too short";
  const header = decode(image.subarray(0, 0x40));
  if (crc16(header.subarray(2)) !== u16(header, 0)) return "header CRC";
  const count = u16(header, 8);
  if (count < 1 || count > 8 || image.length < 0x40 + 0x50 * count) return "file list";
  const list = image.subarray(0x40, 0x40 + 0x50 * count);
  if (crc16(list) !== u16(header, 2)) return "file list CRC";
  for (let i = 0; i < count; i++) {
    const entry = decode(list.subarray(i * 0x50, (i + 1) * 0x50));
    const offset = u32(entry, 8), size = u32(entry, 12);
    if (offset > image.length || size > image.length - offset) return "file cut short";
    if (crc16(image.subarray(offset, offset + size)) !== u16(entry, 4)) return "file CRC";
  }
  return null;
}

export function validatePackage(bytes) {
  const product = productOf(bytes);
  if (!/^[^_]+_\d+$/.test(product)) throw new Error(`not an FM-1 package (identity ${JSON.stringify(product)})`);
  if (!bytes.some((b, i) => b === LOADER_MARK[0] && LOADER_MARK.every((m, j) => bytes[i + j] === m)))
    throw new Error("no Felucca update loader in this package");
  const image = logicalImage(bytes);
  const damage = packageDamage(image);
  if (damage) throw new Error(`damaged package (${damage})`);
  return { product, image };
}

// The official FM-1 V15 (M-VAVE Downloads -> PC Firmware -> FM-1 V15, FM-1.fwsc), for the return to the
// official firmware: only that exact file is accepted (its SHA-256 pins every byte: the loader, the app, the
// layout; the name of the file proves nothing). The digest is the one Felucca 1.0 pins.
export const STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a";
export const STOCK_V15_SIZE = 699956;
export const STOCK_V15_PRODUCT = "FM-1_015";

export async function sha256hex(bytes) {
  const d = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(d)].map((x) => x.toString(16).padStart(2, "0")).join("");
}

export async function validateStockPackage(bytes) {
  if (bytes.length !== STOCK_V15_SIZE || await sha256hex(bytes) !== STOCK_V15_SHA256 || productOf(bytes) !== STOCK_V15_PRODUCT)
    throw new Error("select the unmodified official FM-1 V15 file (FM-1.fwsc)");
  return { product: STOCK_V15_PRODUCT, image: logicalImage(bytes) };
}

