"""Setup configuration for pyghidra-mcp fork."""

from setuptools import setup, find_packages

setup(
    name="pyghidra-mcp",
    version="0.1.6",
    author="clearbluejar",
    author_email="3752074+clearbluejar@users.noreply.github.com",
    description="Python Command-Line Ghidra MCP",
    long_description_content_type="text/markdown",
    url="https://github.com/clearbluejar/pyghidra-mcp",
    packages=find_packages(),
    python_requires=">=3.10",
    install_requires=[
        "click>=8.2.1",
        "mcp[cli]>=1.9.4",
        "pyghidra>=2.1.0",
        "pydantic>=2.0.0",
    ],
    extras_require={
        "dev": [
            "aiohttp>=3.9.5",
            "mcp[cli]>=1.11.0",
            "pre-commit>=3.0.0",
            "pyright>=1.1.0",
            "pytest-asyncio>=0.23.0",
            "pytest-cov>=6.2.1",
            "pytest>=8.4.1",
            "ruff>=0.11.4",
            "tomli-w>=1.0.0",
            "tomli>=2.0.1",
        ]
    },
    entry_points={
        "console_scripts": [
            "pyghidra-mcp=pyghidra_mcp.server:main",
        ],
    },
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Topic :: Security",
    ],
    keywords=[
        "dynamic-analysis",
        "ghidra",
        "mcp",
        "pyghidra",
        "reverse-engineering",
        "security",
        "static-analysis",
    ],
)
