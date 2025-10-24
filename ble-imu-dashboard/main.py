import asyncio
from qasync import QEventLoop
from PyQt6.QtWidgets import QApplication
from ui.main_window import IMUDashboard

def main():
    app = QApplication([])
    loop = QEventLoop(app)
    asyncio.set_event_loop(loop)

    window = IMUDashboard(loop)
    window.show()

    with loop:
        loop.run_forever()

if __name__ == "__main__":
    main()
