"""Laser Chat: desktop app for the ESP32 optical link."""
import sys

from PySide6.QtWidgets import QApplication

from laserchat.main_window import MainWindow, apply_dark_theme


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Laser Chat")
    apply_dark_theme(app)
    win = MainWindow()
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
