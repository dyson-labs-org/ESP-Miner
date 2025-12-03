# Discovering Auradine Chip ID and Response Length

## Current Problem

The skeleton driver uses placeholder values:
```c
#define AURADINE_CHIP_ID 0xAD00                    // ← WRONG!
#define AURADINE_CHIP_ID_RESPONSE_LENGTH 11        // ← PROBABLY WRONG!
```

These MUST be corrected for chip detection to work.

## What Will Happen When You Boot

### Scenario 1: Wrong CHIP_ID, Correct Response Length

**Expected Log Output:**
```
I (xxx) auradine: ============================================
I (xxx) auradine:    AURADINE TREASURE INITIALIZATION
I (xxx) auradine: ============================================
I (xxx) auradine: Expected CHIP_ID: 0xAD00 (MAY BE WRONG!)
I (xxx) auradine: Expected response length: 11 bytes (MAY BE WRONG!)
I (xxx) auradine: Step 1: Setting version mask...
I (xxx) auradine: Step 2: Sending CHIP_ID read command...
I (xxx) auradine:   Register: 0x00
I (xxx) auradine: Step 3: Detecting chips...
I (xxx) auradine:   Watch for CHIP_ID mismatch warnings below!
I (xxx) auradine:   The actual chip ID will be in the hex dump.
W (xxx) common: CHIP_ID response mismatch: expected 0xAD00, got 0xXXXX
                                                                   ↑↑↑↑
                                                        ACTUAL CHIP ID - WRITE THIS DOWN!
W (xxx) common: 0xAA 0x55 0xXX 0xXX 0xYY 0xZZ 0x.. 0x.. 0x.. 0x.. 0x..
                ↑-preamble  ↑-chip ID ↑-other data...
                            bytes 2-3
W (xxx) auradine: No Auradine chips detected
```

**What to do:**
1. Note the **actual chip ID** from the mismatch message: `got 0xXXXX`
2. Update `components/asic/auradine.c:21`:
   ```c
   #define AURADINE_CHIP_ID 0xXXXX  // ← Use the actual value you saw
   ```

### Scenario 2: Wrong Response Length

**Expected Log Output:**
```
I (xxx) auradine: Step 3: Detecting chips...
E (xxx) common: Invalid CHIP_ID response length: expected 11, got 9
                                                                   ↑
                                                    ACTUAL LENGTH - WRITE THIS DOWN!
E (xxx) common: 0xAA 0x55 0x?? 0x?? 0x?? 0x?? 0x?? 0x?? 0x??
                ↑-----------------------------------------↑
                        Count these bytes (9 in this example)
W (xxx) auradine: No Auradine chips detected
```

**What to do:**
1. Note the **actual length** from the error message: `got 9`
2. Count the bytes in the hex dump to verify
3. Update `components/asic/auradine.c:22`:
   ```c
   #define AURADINE_CHIP_ID_RESPONSE_LENGTH 9  // ← Use actual value
   ```

### Scenario 3: Both Wrong (Most Likely!)

You'll see **BOTH** types of errors. Fix them one at a time:

**Step 1**: Fix response length first (so you can see complete responses)
**Step 2**: Fix chip ID (so chip gets detected)
**Step 3**: Rebuild and test

## Response Format Reference

Standard chip ID response format (from Bitmain chips):
```
Byte 0-1:  Preamble (0xAA 0x55)
Byte 2-3:  Chip ID (16-bit, big-endian)
Byte 4:    Core count / Config byte
Byte 5:    Chip address
Byte 6-9:  Additional chip info
Byte 10:   CRC5 checksum
```

**Total**: 11 bytes (for Bitmain)

Auradine **might** use:
- Same format (11 bytes)
- Shorter format (9 bytes, no extra info)
- Longer format (13+ bytes, more chip info)
- **Different protocol entirely!**

## How to Update the Code

### After Discovering Correct Values

1. **Edit `components/asic/auradine.c` lines 21-22**:
   ```c
   #define AURADINE_CHIP_ID 0xXXXX              // ← Update with actual chip ID
   #define AURADINE_CHIP_ID_RESPONSE_LENGTH NN  // ← Update with actual length
   ```

2. **Rebuild**:
   ```bash
   idf.py build
   ```

3. **Flash and Test**:
   ```bash
   idf.py flash monitor
   ```

4. **Verify Success**:
   ```
   I (xxx) common: Chip 0 detected: CORE_NUM: 0xYY ADDR: 0x00
   I (xxx) common: Chip 1 detected: CORE_NUM: 0xYY ADDR: 0x80
   I (xxx) auradine: ╔═══════════════════════════════════════════╗
   I (xxx) auradine: ║  CHIP DETECTION SUCCESS!                  ║
   I (xxx) auradine: ╠═══════════════════════════════════════════╣
   I (xxx) auradine: ║  Detected: 2 chip(s)                      ║
   ```

## If STILL No Response

### Check Communication

1. **UART not working** - Check physical connections:
   - TX/RX pins correct?
   - Baud rate correct?
   - Chips powered?

2. **Wrong register address** - Try different registers:
   ```c
   #define AURADINE_CHIP_ID_REG 0x00  // Try 0x00, 0x04, 0x08, etc.
   ```

3. **Wrong command format** - Auradine might use different protocol:
   - Different preamble (not 0x55 0xAA)
   - Different command structure
   - Need datasheet!

4. **Serial debug** - Enable verbose logging:
   ```c
   // In components/asic/include/auradine.h
   #define AURADINE_SERIALTX_DEBUG true
   #define AURADINE_SERIALRX_DEBUG true
   ```

   This will show EVERY byte sent/received.

## Expected Workflow

```
Boot → Config Loaded → ASIC Init → Chip ID Read → Response Analysis
                                                         ↓
                                                    (Wrong values)
                                                         ↓
                                    Log shows actual chip ID & length
                                                         ↓
                                              Update auradine.c
                                                         ↓
                                                Rebuild & Flash
                                                         ↓
                                            Chip Detection Success!
```

## Pro Tip: Capture Full Output

When testing, capture the full serial output:
```bash
idf.py monitor | tee auradine_boot.log
```

Then you can analyze it carefully offline to extract:
- Actual chip ID
- Actual response length
- Response format/structure
- Any other useful info

## Next Steps After Chip Detection Works

1. ✅ Chip ID and response length correct
2. ⏭ Verify register map (see IMPLEMENTORS_AURADINE.md)
3. ⏭ Test initialization sequence
4. ⏭ Discover core counts
5. ⏭ Verify voltage_domains setting
6. ⏭ Test mining!
