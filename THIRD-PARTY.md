# Third-Party Libraries and Licenses

This project incorporates several third-party libraries. Their respective licenses are listed below.

## 1. CDT (Constrained Delaunay Triangulation)
- **Author**: Artëm S. Tashkinov
- **License**: Mozilla Public License v. 2.0 (MPL 2.0)
- **Source**: [https://github.com/Art-Stea1th/CDT](https://github.com/Art-Stea1th/CDT)
- **Files**: `include/core/cdt/*.h`, `include/core/cdt/*.hpp` (except `predicates.h`), `include/core/KDTree.h` (derived work)

## 2. Geometric Predicates
- **Author**: William C. Lenthe
- **License**: BSD 3-Clause License
- **Source**: Included as part of the CDT library or inspired by Jonathan Richard Shewchuk's robust predicates.
- **Files**: `include/core/cdt/predicates.h`

### BSD 3-Clause License (Predicates)
Copyright (c) 2019, William C. Lenthe
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## 3. Qt Framework
- **License**: LGPL v3 / Commercial
- **Usage**: Used for the graphical user interface (QML) and core application logic.
