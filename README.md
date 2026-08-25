# LabelGeneratorGUI - Technical Documentation

**Version:** 1.0
**Author:** Filip Cieśla  
**Platform:** Windows 10/11 (Standalone / Loose Files Deployment)  
**Framework:** Windows App SDK (WinUI 3) / C++ WinRT  

---

## 1. Overview

**LabelGeneratorGUI** is a highly optimized, self-contained desktop application designed to automate the mass production of customized, print-ready PDF product and care labels.

Instead of relying on bloated third-party libraries for core functionalities, the application utilizes native Windows APIs and pure C++ implementations. It seamlessly bridges raw Excel order data with a highly flexible JSON-based ruleset to generate thousands of dynamically styled labels in seconds.

---

## 2. System Architecture & Label Engine Operations

The core of the application is the custom-built **Label Engine**, which operates without external heavy dependencies like Poppler or PDFium.

### 2.1. Raw PDF Generation Engine
The PDF rendering engine is implemented entirely from scratch in C++. It manually constructs the PDF file structure byte by byte:
*   **Object Management:** The engine dynamically assigns object IDs, creates PDF dictionaries (e.g., `<< /Type /Page >>`), and builds the XREF (cross-reference) table required for valid PDF structure.
*   **Coordinate System:** The layout relies on a strict X/Y coordinate system (bottom-left origin), recalculating vertical offsets dynamically based on the `layout` array defined in `config.json`.
*   **Stream Compression:** Text and graphics operations (like `BT`, `ET`, `Tm` for text matrices) are written as raw streams.

### 2.2. Image Processing via GDI+
To embed logos and care icons, the engine uses native Windows GDI+:
*   Images (PNG/JPG) are loaded into memory.
*   The engine calculates the correct aspect ratio and scales the bounding boxes according to the PDF's point system.
*   The raw pixel data is extracted, converted, and injected directly into the PDF object streams as XObjects.

### 2.3. Data Ingestion (Excel to CSV)
To avoid the overhead of C++ Excel SDKs, the engine executes a lightweight, hidden PowerShell COM interop script. This script rapidly converts the target `.xlsx` worksheet into a `.csv` file in a temporary location, allowing the C++ backend to parse the rows sequentially at maximum speed.

---

## 3. Configuration & Rule Mapping (`config.json`)

The flexibility of the engine is governed by the `config.json` file. The engine processes rules in the following sequence:

1.  **Style Normalization (`style_mapping`):** Raw Excel style inputs are mapped to standard base keys (e.g., `MEN'S SHIRT` -> `SHIRT`).
2.  **Priority-Based Styling (`customer_styles`):** The app evaluates `customer_styles` using a "First Match" logic. It checks for exact overrides (e.g., `CUSTOMER AND SHIRT`), falls back to single entities (`CUSTOMER` or `SHIRT`), and defaults to `DEFAULT` if no match is found. Controls visibility toggles (`show_*`) and color schemes.
3.  **Translations:** Standardized styles are translated via `style_translations`, and material codes via `material_translations`.
4.  **Logo Matching:** Excel product codes and customer names are matched against `logo_mapping` to select the correct physical image files.
5.  **Dynamic Layout Execution:** The `layout` array determines the vertical order, spacing, and font sizes for every printed block (`logo`, `style`, `product`, `size`, `care_header`, `care_symbols`, `composition`, `lining`) on the canvas.

---

## 4. References & Additional Documentation

For detailed instructions on using and modifying the application, please refer to the supplementary text files included in the project:

*   **`edycja_konfiguracji.txt`**  
    Comprehensive manual (in Polish) for editing the `config.json` file, explaining syntax rules (JSON comma rules) and property definitions to prevent critical crashes.
*   **`credits.txt`**  
    Information about the author, version details, and a list of utilized third-party libraries (e.g., `nlohmann/json`).

---

## 5. Deployment & Execution

The application runs as a standalone desktop utility. To launch the program:
1. Ensure all required dependency libraries and resources are located in the application directory alongside `LabelGeneratorGUI.exe`.
2. Use the provided shortcut or run `LabelGeneratorGUI.exe` directly.