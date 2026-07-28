import sys


if sys.platform == "win32" and not sys.argv[1:]:
    from cardbridge.windows_app import main
else:
    from cardbridge.__main__ import main


if __name__ == "__main__":
    main()
