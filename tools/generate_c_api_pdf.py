"""Generate the Chinese C API guide PDF and append verified sample result images."""

from __future__ import annotations

import html
import re
import shutil
from pathlib import Path

from PIL import Image
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (
    Image as PdfImage,
    KeepTogether,
    PageBreak,
    Paragraph,
    Preformatted,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "docs" / "WaferCalibSDK_C_API调用说明.md"
OUTPUT = ROOT / "output" / "pdf" / "WaferCalibSDK_C_API调用说明.pdf"
ASSET_DIR = ROOT / "tmp" / "pdfs" / "c_api_result_images"
FONT = Path(r"C:\Windows\Fonts\simhei.ttf")

RESULT_IMAGES = [
    ("背景噪声模板（C++ 示例）", ROOT / "images" / "去噪声" / "output_dark_template.bmp"),
    ("背景噪声模板（C API 示例）", ROOT / "images" / "去噪声" / "output_dark_template_c_api.bmp"),
    ("四个十字 Mark 中心检测结果", ROOT / "images" / "根据四个十字Mark求中心（兼容不同倍率，Mark大小不一样）" / "output_mark_center.bmp"),
    ("水平线角度检测结果 - 视野 1", ROOT / "images" / "根据线输出角度" / "output_angle_adjustment_1.bmp"),
    ("水平线角度检测结果 - 视野 2", ROOT / "images" / "根据线输出角度" / "output_angle_adjustment_2.bmp"),
    ("点阵畸变模板建立诊断图", ROOT / "images" / "畸变矫正" / "output_distortion_detection.bmp"),
    ("点阵畸变校正结果", ROOT / "images" / "畸变矫正" / "output_distortion_corrected.bmp"),
]


def register_font() -> str:
    if not FONT.exists():
        raise FileNotFoundError(f"缺少中文字体: {FONT}")
    pdfmetrics.registerFont(TTFont("WaferChinese", str(FONT)))
    return "WaferChinese"


def prepare_images() -> list[tuple[str, Path, tuple[int, int]]]:
    """Downsample result BMPs to sharp, compact JPEGs for the PDF."""
    if ASSET_DIR.exists():
        shutil.rmtree(ASSET_DIR)
    ASSET_DIR.mkdir(parents=True)
    prepared: list[tuple[str, Path, tuple[int, int]]] = []
    for index, (caption, source) in enumerate(RESULT_IMAGES, start=1):
        if not source.exists():
            raise FileNotFoundError(f"缺少测试结果图: {source}")
        with Image.open(source) as image:
            image = image.convert("RGB")
            image.thumbnail((1500, 1500), Image.Resampling.LANCZOS)
            destination = ASSET_DIR / f"{index:02d}.jpg"
            image.save(destination, "JPEG", quality=92, optimize=True)
            prepared.append((caption, destination, image.size))
    return prepared


def make_styles(font_name: str) -> dict[str, ParagraphStyle]:
    styles = getSampleStyleSheet()
    return {
        "title": ParagraphStyle(
            "WaferTitle", parent=styles["Title"], fontName=font_name,
            fontSize=21, leading=29, alignment=TA_CENTER, textColor=colors.HexColor("#15395B"), spaceAfter=10 * mm,
        ),
        "h1": ParagraphStyle(
            "WaferH1", parent=styles["Heading1"], fontName=font_name,
            fontSize=15, leading=21, textColor=colors.HexColor("#15395B"), spaceBefore=7 * mm, spaceAfter=3 * mm,
        ),
        "h2": ParagraphStyle(
            "WaferH2", parent=styles["Heading2"], fontName=font_name,
            fontSize=12, leading=18, textColor=colors.HexColor("#1D5D86"), spaceBefore=5 * mm, spaceAfter=2.5 * mm,
        ),
        "body": ParagraphStyle(
            "WaferBody", parent=styles["BodyText"], fontName=font_name,
            fontSize=9.2, leading=15, textColor=colors.HexColor("#222222"), spaceAfter=2.3 * mm,
        ),
        "bullet": ParagraphStyle(
            "WaferBullet", parent=styles["BodyText"], fontName=font_name,
            fontSize=9.2, leading=15, leftIndent=5 * mm, firstLineIndent=-3.5 * mm, spaceAfter=1.2 * mm,
        ),
        "code": ParagraphStyle(
            "WaferCode", parent=styles["Code"], fontName=font_name, fontSize=6.9, leading=9.2,
            textColor=colors.HexColor("#253746"), backColor=colors.HexColor("#F3F6F8"), borderPadding=3 * mm,
            borderColor=colors.HexColor("#DCE5EA"), borderWidth=0.5, spaceAfter=3 * mm,
        ),
        "caption": ParagraphStyle(
            "WaferCaption", parent=styles["BodyText"], fontName=font_name,
            fontSize=8.4, leading=12, alignment=TA_CENTER, textColor=colors.HexColor("#4E6170"), spaceBefore=1.5 * mm, spaceAfter=5 * mm,
        ),
        "small": ParagraphStyle(
            "WaferSmall", parent=styles["BodyText"], fontName=font_name,
            fontSize=8, leading=12, textColor=colors.HexColor("#556775"),
        ),
    }


def inline(text: str) -> str:
    escaped = html.escape(text.strip())
    return re.sub(r"`([^`]+)`", r'<font name="Courier" color="#243746">\1</font>', escaped)


def parse_table(lines: list[str], styles: dict[str, ParagraphStyle]) -> Table:
    rows = []
    for source in lines:
        cells = [cell.strip() for cell in source.strip().strip("|").split("|")]
        if all(re.fullmatch(r":?-{3,}:?", cell) for cell in cells):
            continue
        rows.append([Paragraph(inline(cell), styles["small"]) for cell in cells])
    column_count = max(len(row) for row in rows)
    usable_width = A4[0] - 32 * mm
    widths = [usable_width / column_count] * column_count
    table = Table(rows, colWidths=widths, repeatRows=1, hAlign="LEFT")
    table.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#DCEBF5")),
        ("TEXTCOLOR", (0, 0), (-1, 0), colors.HexColor("#15395B")),
        ("FONTNAME", (0, 0), (-1, 0), "WaferChinese"),
        ("GRID", (0, 0), (-1, -1), 0.35, colors.HexColor("#B8C8D3")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("RIGHTPADDING", (0, 0), (-1, -1), 2.2 * mm),
        ("TOPPADDING", (0, 0), (-1, -1), 1.5 * mm),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 1.5 * mm),
    ]))
    return table


def markdown_story(markdown: str, styles: dict[str, ParagraphStyle]):
    story = []
    lines = markdown.splitlines()
    index = 0
    while index < len(lines):
        line = lines[index]
        if not line.strip():
            index += 1
            continue
        if line.startswith("```"):
            index += 1
            code = []
            while index < len(lines) and not lines[index].startswith("```"):
                code.append(lines[index])
                index += 1
            story.append(Preformatted("\n".join(code), styles["code"], maxLineLength=105))
            index += 1
            continue
        if line.startswith("# "):
            story.append(Paragraph(inline(line[2:]), styles["title"]))
        elif line.startswith("## "):
            story.append(Paragraph(inline(line[3:]), styles["h1"]))
        elif line.startswith("### "):
            story.append(Paragraph(inline(line[4:]), styles["h2"]))
        elif line.startswith("|"):
            table_lines = []
            while index < len(lines) and lines[index].startswith("|"):
                table_lines.append(lines[index])
                index += 1
            story.append(parse_table(table_lines, styles))
            story.append(Spacer(1, 2.4 * mm))
            continue
        elif line.startswith("- "):
            story.append(Paragraph("• " + inline(line[2:]), styles["bullet"]))
        else:
            story.append(Paragraph(inline(line), styles["body"]))
        index += 1
    return story


def footer(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor("#B8C8D3"))
    canvas.line(16 * mm, 13 * mm, A4[0] - 16 * mm, 13 * mm)
    canvas.setFont("WaferChinese", 7.8)
    canvas.setFillColor(colors.HexColor("#607888"))
    canvas.drawString(16 * mm, 8.5 * mm, "WaferCalibSDK C API 调用说明")
    canvas.drawRightString(A4[0] - 16 * mm, 8.5 * mm, f"第 {doc.page} 页")
    canvas.restoreState()


def build_pdf() -> None:
    font_name = register_font()
    styles = make_styles(font_name)
    prepared_images = prepare_images()
    document = SimpleDocTemplate(
        str(OUTPUT), pagesize=A4, leftMargin=16 * mm, rightMargin=16 * mm,
        topMargin=16 * mm, bottomMargin=18 * mm, title="WaferCalibSDK C API 调用说明",
        author="WaferCalibSDK",
    )
    story = markdown_story(SOURCE.read_text(encoding="utf-8"), styles)
    story.append(PageBreak())
    story.append(Paragraph("7. 示例测试结果图", styles["h1"]))
    story.append(Paragraph("以下图片为当前 SDK 示例程序生成的结果图，用于展示接口输出与标注方式。", styles["body"]))
    max_width = A4[0] - 32 * mm
    max_height = 156 * mm
    for caption, image_path, (width, height) in prepared_images:
        ratio = min(max_width / width, max_height / height)
        image = PdfImage(str(image_path), width=width * ratio, height=height * ratio)
        block = [image, Paragraph(caption, styles["caption"])]
        story.append(KeepTogether(block))
    document.build(story, onFirstPage=footer, onLaterPages=footer)


if __name__ == "__main__":
    build_pdf()
    print(OUTPUT)
