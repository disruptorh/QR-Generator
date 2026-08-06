 #!/usr/bin/env python3
"""
╔══════════════════════════════════════╗
║   QR Generator — 100% Offline       ║
║   Datos embebidos. Sin internet.    ║
╚══════════════════════════════════════╝
Dependencias: pip install qrcode[pil] pillow
"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import qrcode
from PIL import Image, ImageTk
import os
import sys

# ─── Paleta de colores ───────────────────────────────────────────
BG_DARK     = "#0d0d0d"
BG_PANEL    = "#141414"
BG_CARD     = "#1a1a1a"
ACCENT      = "#00ff88"
ACCENT_DIM  = "#00cc6a"
TEXT_MAIN   = "#f0f0f0"
TEXT_MUTED  = "#666666"
BORDER      = "#2a2a2a"
ERROR_RED   = "#ff4444"
WARN_AMBER  = "#ffaa00"

# ─── Tipos de QR disponibles ────────────────────────────────────
TIPOS = {
    "Texto libre": {
        "placeholder": "Escribe cualquier texto aquí...",
        "template": "{texto}",
        "campos": ["texto"],
        "labels": ["Contenido"],
    },
    "WiFi": {
        "placeholder": "Nombre de red",
        "template": "WIFI:T:{seguridad};S:{ssid};P:{password};;",
        "campos": ["ssid", "password", "seguridad"],
        "labels": ["Nombre de red (SSID)", "Contraseña", "Seguridad"],
    },
    "Contacto (vCard)": {
        "placeholder": "Nombre completo",
        "template": (
            "BEGIN:VCARD\nVERSION:3.0\n"
            "FN:{nombre}\nTEL:{telefono}\nEMAIL:{email}\nEND:VCARD"
        ),
        "campos": ["nombre", "telefono", "email"],
        "labels": ["Nombre completo", "Teléfono", "Email"],
    },
    "Coordenadas GPS": {
        "placeholder": "-33.4489",
        "template": "geo:{latitud},{longitud}",
        "campos": ["latitud", "longitud"],
        "labels": ["Latitud", "Longitud"],
    },
}

# ─── Colores del QR ─────────────────────────────────────────────
QR_COLORES = {
    "Clásico (negro/blanco)": ("#000000", "#ffffff"),
    "Matrix (verde neón)":    ("#00ff88", "#0d0d0d"),
    "Ciberpunk (cyan/negro)": ("#00eaff", "#0a0a1a"),
    "Ámbar retro":            ("#ffaa00", "#1a1000"),
    "Rojo oscuro":            ("#cc2200", "#1a0000"),
}


class QRGeneratorApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("QR Generator — Offline")
        self.root.configure(bg=BG_DARK)
        self.root.resizable(False, False)

        # Estado
        self.tipo_var       = tk.StringVar(value="Texto libre")
        self.color_var      = tk.StringVar(value="Clásico (negro/blanco)")
        self.correccion_var = tk.StringVar(value="M — Media (recomendado)")
        self.tamaño_var     = tk.IntVar(value=10)
        self.borde_var      = tk.IntVar(value=4)

        self.campos_entries: dict[str, tk.Widget] = {}
        self.qr_photo: ImageTk.PhotoImage | None  = None
        self.last_img: Image.Image | None          = None
        self.script_dir = os.path.dirname(os.path.abspath(__file__))

        self._build_ui()
        self._on_tipo_change()

    # ════════════════════════════════════════════════════════════
    #  CONSTRUCCIÓN DE LA UI
    # ════════════════════════════════════════════════════════════

    def _build_ui(self):
        # ── Cabecera ──────────────────────────────────────────
        header = tk.Frame(self.root, bg=BG_DARK)
        header.pack(fill="x", padx=24, pady=(20, 0))

        tk.Label(
            header, text="▪ QR", font=("Courier New", 22, "bold"),
            fg=ACCENT, bg=BG_DARK
        ).pack(side="left")
        tk.Label(
            header, text=" GENERATOR", font=("Courier New", 22, "bold"),
            fg=TEXT_MAIN, bg=BG_DARK
        ).pack(side="left")
        tk.Label(
            header, text="100% OFFLINE", font=("Courier New", 9),
            fg=TEXT_MUTED, bg=BG_DARK
        ).pack(side="right", pady=(8, 0))

        sep = tk.Frame(self.root, height=1, bg=BORDER)
        sep.pack(fill="x", padx=24, pady=12)

        # ── Cuerpo: columnas izq / der ────────────────────────
        body = tk.Frame(self.root, bg=BG_DARK)
        body.pack(fill="both", padx=24, pady=(0, 20))

        left  = tk.Frame(body, bg=BG_DARK, width=340)
        left.pack(side="left", fill="y", padx=(0, 16))
        left.pack_propagate(False)

        right = tk.Frame(body, bg=BG_DARK)
        right.pack(side="left", fill="both", expand=True)

        self._build_left(left)
        self._build_right(right)

    def _card(self, parent, title):
        """Devuelve un Frame con borde y título."""
        outer = tk.Frame(parent, bg=BORDER, bd=0)
        outer.pack(fill="x", pady=(0, 12))

        inner = tk.Frame(outer, bg=BG_CARD, bd=0)
        inner.pack(fill="x", padx=1, pady=1)

        tk.Label(
            inner, text=title, font=("Courier New", 8, "bold"),
            fg=ACCENT, bg=BG_CARD, anchor="w", padx=12, pady=8
        ).pack(fill="x")

        tk.Frame(inner, height=1, bg=BORDER).pack(fill="x")

        content = tk.Frame(inner, bg=BG_CARD)
        content.pack(fill="x", padx=12, pady=10)
        return content

    # ─── Columna izquierda ───────────────────────────────────

    def _build_left(self, parent):
        # Tipo de QR
        c1 = self._card(parent, "▸ TIPO DE DATOS")
        self._combo(c1, "Tipo", self.tipo_var, list(TIPOS.keys()), self._on_tipo_change)

        # Campos dinámicos
        self.campos_frame = self._card(parent, "▸ CONTENIDO")

        # Opciones avanzadas
        c3 = self._card(parent, "▸ OPCIONES")
        self._combo(
            c3, "Color", self.color_var, list(QR_COLORES.keys())
        )
        correcciones = [
            "L — Baja (datos pequeños)",
            "M — Media (recomendado)",
            "Q — Alta",
            "H — Máxima (robustez)",
        ]
        self._combo(c3, "Corrección de error", self.correccion_var, correcciones)
        self._slider(c3, "Tamaño de pixel", self.tamaño_var, 5, 20)
        self._slider(c3, "Borde (módulos)",  self.borde_var,  1, 10)

    def _combo(self, parent, label, var, values, cmd=None):
        tk.Label(
            parent, text=label, font=("Courier New", 8),
            fg=TEXT_MUTED, bg=BG_CARD, anchor="w"
        ).pack(fill="x", pady=(4, 1))

        style = ttk.Style()
        style.theme_use("default")
        style.configure(
            "Dark.TCombobox",
            fieldbackground=BG_PANEL,
            background=BG_PANEL,
            foreground=TEXT_MAIN,
            selectbackground=ACCENT_DIM,
            selectforeground=BG_DARK,
            arrowcolor=ACCENT,
            bordercolor=BORDER,
            lightcolor=BORDER,
            darkcolor=BORDER,
        )
        cb = ttk.Combobox(
            parent, textvariable=var, values=values,
            state="readonly", style="Dark.TCombobox",
            font=("Courier New", 9)
        )
        cb.pack(fill="x", pady=(0, 4))
        if cmd:
            cb.bind("<<ComboboxSelected>>", lambda _: cmd())

    def _slider(self, parent, label, var, mn, mx):
        row = tk.Frame(parent, bg=BG_CARD)
        row.pack(fill="x", pady=2)
        tk.Label(
            row, text=label, font=("Courier New", 8),
            fg=TEXT_MUTED, bg=BG_CARD, anchor="w", width=22
        ).pack(side="left")
        val_lbl = tk.Label(
            row, textvariable=var, font=("Courier New", 8, "bold"),
            fg=ACCENT, bg=BG_CARD, width=3
        )
        val_lbl.pack(side="right")
        tk.Scale(
            parent, from_=mn, to=mx, orient="horizontal",
            variable=var, showvalue=False,
            bg=BG_CARD, fg=ACCENT, troughcolor=BORDER,
            activebackground=ACCENT, highlightthickness=0, bd=0
        ).pack(fill="x")

    # ─── Columna derecha ─────────────────────────────────────

    def _build_right(self, parent):
        # Canvas de previsualización
        self.canvas_frame = tk.Frame(parent, bg=BG_PANEL, bd=0)
        self.canvas_frame.pack(fill="both", expand=True)

        tk.Label(
            self.canvas_frame, text="▸ PREVISUALIZACIÓN",
            font=("Courier New", 8, "bold"), fg=ACCENT, bg=BG_PANEL,
            anchor="w", padx=12, pady=8
        ).pack(fill="x")
        tk.Frame(self.canvas_frame, height=1, bg=BORDER).pack(fill="x")

        self.canvas = tk.Canvas(
            self.canvas_frame, width=300, height=300,
            bg=BG_PANEL, highlightthickness=0
        )
        self.canvas.pack(padx=20, pady=20)
        self._draw_placeholder()

        # Barra de estado
        self.status_var = tk.StringVar(value="Listo.")
        self.status_lbl = tk.Label(
            self.canvas_frame, textvariable=self.status_var,
            font=("Courier New", 8), fg=TEXT_MUTED, bg=BG_PANEL,
            anchor="w", padx=12, pady=6
        )
        self.status_lbl.pack(fill="x")

        # Botones
        btn_frame = tk.Frame(parent, bg=BG_DARK)
        btn_frame.pack(fill="x", pady=(10, 0))

        self._btn(btn_frame, "⬡  GENERAR QR",   ACCENT,    BG_DARK,  self._generar, True)
        self._btn(btn_frame, "↓  GUARDAR PNG",  BG_CARD,   ACCENT,   self._guardar, False)

    def _btn(self, parent, text, bg, fg, cmd, primary):
        b = tk.Button(
            parent, text=text, command=cmd,
            font=("Courier New", 10, "bold"),
            bg=bg, fg=fg, activebackground=ACCENT_DIM,
            activeforeground=BG_DARK,
            relief="flat", cursor="hand2",
            pady=10, bd=0,
        )
        b.pack(fill="x", pady=3)
        if primary:
            b.configure(bg=ACCENT, fg=BG_DARK)

    def _draw_placeholder(self):
        self.canvas.delete("all")
        self.canvas.create_rectangle(
            0, 0, 300, 300, fill=BG_PANEL, outline=""
        )
        # cuadrícula decorativa
        for i in range(0, 301, 30):
            self.canvas.create_line(i, 0, i, 300, fill=BORDER, width=1)
            self.canvas.create_line(0, i, 300, i, fill=BORDER, width=1)
        self.canvas.create_text(
            150, 140, text="[ sin datos ]",
            font=("Courier New", 12), fill=TEXT_MUTED
        )
        self.canvas.create_text(
            150, 162, text="Completa el formulario\ny presiona GENERAR",
            font=("Courier New", 8), fill=TEXT_MUTED, justify="center"
        )

    # ════════════════════════════════════════════════════════════
    #  CAMPOS DINÁMICOS
    # ════════════════════════════════════════════════════════════

    def _on_tipo_change(self):
        # Limpiar campos anteriores
        for w in self.campos_frame.winfo_children():
            w.destroy()
        self.campos_entries.clear()

        tipo = self.tipo_var.get()
        cfg  = TIPOS[tipo]

        if tipo == "WiFi":
            self._build_wifi_fields(cfg)
        else:
            for campo, label in zip(cfg["campos"], cfg["labels"]):
                tk.Label(
                    self.campos_frame, text=label,
                    font=("Courier New", 8), fg=TEXT_MUTED,
                    bg=BG_CARD, anchor="w"
                ).pack(fill="x", pady=(4, 1))
                e = tk.Text(
                    self.campos_frame, height=3,
                    font=("Courier New", 9),
                    bg=BG_PANEL, fg=TEXT_MAIN,
                    insertbackground=ACCENT,
                    relief="flat", bd=0,
                    selectbackground=ACCENT_DIM, selectforeground=BG_DARK,
                    padx=6, pady=4
                )
                e.pack(fill="x", pady=(0, 4))
                self.campos_entries[campo] = e

    def _build_wifi_fields(self, cfg):
        labels = cfg["labels"]
        campos = cfg["campos"]

        for campo, label in zip(campos[:2], labels[:2]):
            tk.Label(
                self.campos_frame, text=label,
                font=("Courier New", 8), fg=TEXT_MUTED,
                bg=BG_CARD, anchor="w"
            ).pack(fill="x", pady=(4, 1))
            e = tk.Entry(
                self.campos_frame,
                font=("Courier New", 9),
                bg=BG_PANEL, fg=TEXT_MAIN,
                insertbackground=ACCENT,
                relief="flat", bd=0,
            )
            e.pack(fill="x", pady=(0, 4), ipady=5, ipadx=6)
            self.campos_entries[campo] = e

        # Seguridad
        tk.Label(
            self.campos_frame, text=labels[2],
            font=("Courier New", 8), fg=TEXT_MUTED,
            bg=BG_CARD, anchor="w"
        ).pack(fill="x", pady=(4, 1))
        seg_var = tk.StringVar(value="WPA")
        self.campos_entries["seguridad"] = seg_var
        row = tk.Frame(self.campos_frame, bg=BG_CARD)
        row.pack(fill="x")
        for opt in ("WPA", "WEP", "nopass"):
            tk.Radiobutton(
                row, text=opt, variable=seg_var, value=opt,
                font=("Courier New", 9), fg=TEXT_MAIN, bg=BG_CARD,
                selectcolor=BG_PANEL, activebackground=BG_CARD,
                activeforeground=ACCENT
            ).pack(side="left", padx=4)

    # ════════════════════════════════════════════════════════════
    #  GENERACIÓN
    # ════════════════════════════════════════════════════════════

    def _get_payload(self) -> str | None:
        tipo = self.tipo_var.get()
        cfg  = TIPOS[tipo]
        vals = {}

        for campo in cfg["campos"]:
            widget = self.campos_entries.get(campo)
            if widget is None:
                vals[campo] = ""
            elif isinstance(widget, tk.StringVar):
                vals[campo] = widget.get().strip()
            elif isinstance(widget, tk.Text):
                vals[campo] = widget.get("1.0", "end").strip()
            else:
                vals[campo] = widget.get().strip()

        # Validación básica
        primero = cfg["campos"][0]
        if not vals.get(primero):
            messagebox.showerror(
                "Campo vacío",
                f"El campo «{cfg['labels'][0]}» no puede estar vacío."
            )
            return None

        return cfg["template"].format(**vals)

    def _get_correccion(self):
        nivel = self.correccion_var.get()[0]
        return {
            "L": qrcode.constants.ERROR_CORRECT_L,
            "M": qrcode.constants.ERROR_CORRECT_M,
            "Q": qrcode.constants.ERROR_CORRECT_Q,
            "H": qrcode.constants.ERROR_CORRECT_H,
        }.get(nivel, qrcode.constants.ERROR_CORRECT_M)

    def _generar(self):
        payload = self._get_payload()
        if payload is None:
            return

        fill_c, back_c = QR_COLORES[self.color_var.get()]

        qr = qrcode.QRCode(
            version=None,
            error_correction=self._get_correccion(),
            box_size=self.tamaño_var.get(),
            border=self.borde_var.get(),
        )
        qr.add_data(payload)
        qr.make(fit=True)

        self.last_img = qr.make_image(fill_color=fill_c, back_color=back_c)

        # Guardar automáticamente junto al script
        out_path = os.path.join(self.script_dir, "qr_output.png")
        self.last_img.save(out_path)

        # Previsualizar en canvas
        preview = self.last_img.resize((280, 280), Image.NEAREST)
        self.qr_photo = ImageTk.PhotoImage(preview)
        self.canvas.delete("all")
        self.canvas.create_image(150, 150, image=self.qr_photo)

        chars = len(payload)
        color_name = self.color_var.get().split("(")[0].strip()
        self._status(
            f"✓ Guardado: qr_output.png  |  {chars} chars  |  {color_name}",
            ACCENT
        )

    def _guardar(self):
        if self.last_img is None:
            messagebox.showinfo("Sin QR", "Genera un QR primero.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".png",
            filetypes=[("PNG", "*.png"), ("Todos", "*.*")],
            initialfile="qr_output.png",
            initialdir=self.script_dir,
        )
        if path:
            self.last_img.save(path)
            self._status(f"✓ Exportado: {os.path.basename(path)}", ACCENT)

    def _status(self, msg: str, color: str = TEXT_MUTED):
        self.status_var.set(msg)
        self.status_lbl.configure(fg=color)


# ════════════════════════════════════════════════════════════════
#  ENTRY POINT
# ════════════════════════════════════════════════════════════════

def main():
    try:
        import qrcode  # noqa: F401
        from PIL import Image  # noqa: F401
    except ImportError:
        print(
            "Faltan dependencias. Ejecuta:\n"
            "  pip install qrcode[pil] pillow"
        )
        sys.exit(1)

    root = tk.Tk()
    root.geometry("700x620")
    root.minsize(700, 620)
    app = QRGeneratorApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
