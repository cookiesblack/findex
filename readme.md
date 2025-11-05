# FIndex - Fast File Integrity Checker

A high-performance, multi-threaded file integrity monitoring tool that uses CRC32 checksums to detect file changes, additions, and deletions.

## Features

- ⚡ **Multi-threaded scanning** - Utilizes all available CPU cores
- 🔍 **CRC32 checksums** - Fast and reliable file integrity verification
- 📊 **Progress indicator** - Real-time progress with animated spinner
- 🎨 **Color output** - Clear, colored diff output (auto-detects TTY)
- 📝 **Detailed logging** - Comprehensive log file with timestamps
- 🎯 **Flexible filtering** - Show only new, modified, deleted, or all changes
- 🖼️ **Media file handling** - Option to exclude media files from indexing
- 💾 **CSV database** - Human-readable database format

## Installation

### Requirements
- GCC or compatible C compiler
- POSIX-compliant system (Linux, macOS, BSD)
- pthread library

### Build
```bash
make
```

### Install (optional)
```bash
sudo make install
```

This installs to `/usr/local/bin` by default. To change the installation prefix:
```bash
sudo make install PREFIX=/opt/local
```

### Uninstall
```bash
sudo make uninstall
```

## Usage

### Basic Workflow

1. **Create initial database:**
```bash
./findex --index
```

2. **Check for changes:**
```bash
./findex --check
```

3. **Update database after review:**
```bash
./findex --reindex
```

### Command Reference

#### Indexing
```bash
# Create database with all files
./findex --index

# Create database excluding media files
./findex --index --exclude-media

# Custom database file
./findex --index --db myproject.db

# Use specific number of threads
./findex --index --threads 8
```

#### Checking
```bash
# Basic check (shows new and modified files)
./findex --check

# Show all changes
./findex --check --filter all

# Show only new files
./findex --check --filter new

# Show only modified files
./findex --check --filter modified

# Show only deleted files
./findex --check --filter deleted

# Custom log file
./findex --check --log /var/log/integrity.log

# Use custom database
./findex --check --db myproject.db
```

#### Reindexing
```bash
# Rebuild database (same as --index)
./findex --reindex
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--index` | Create initial file database | - |
| `--check` | Compare current files against database | - |
| `--reindex` | Rebuild the database | - |
| `--db FILE` | Database file to use | `findex.db` |
| `--log FILE` | Log file for check results | `findex.log` |
| `--threads N` | Number of worker threads | CPU count |
| `--exclude-media` | Exclude media files (for index/reindex) | Include all |
| `--filter FILTER` | Filter output for --check | `created` |

### Filter Options

| Filter | Description |
|--------|-------------|
| `new` | Show only newly created files |
| `modified` | Show only modified files |
| `deleted` | Show only deleted files |
| `all` | Show all changes |
| `created` | Show new and modified files (default) |

### Media File Extensions

The following extensions are considered media files:
- **Images:** jpg, jpeg, png, gif, svg, webp, ico
- **Video:** mp4, webm
- **Audio:** mp3, wav, ogg
- **Documents:** pdf

## Output Examples

### Check Output
```
Scanning current filesystem (fast, 8 threads, filter: all)...
[|] 100.0% (1234/1234)
Comparing with database...
Database: 1230 files (45 media filtered out)
Current:  1235 files (45 media filtered out)
+ NEW: ./src/newfile.c
! MODIFIED: ./config.json
- DELETED: ./old/backup.txt
----------------------------------------
Summary: +1 new, !1 modified, -1 deleted
```

### Log File Format
```
========================================
Check performed: 2025-11-05 14:30:45
========================================
+ NEW: ./src/newfile.c (size=1024 crc=0x12345678)
! MODIFIED: ./config.json
  Old: size=2048 mtime=1699180800 crc=0xabcdef01
  New: size=2100 mtime=1699267200 crc=0xfedcba98
- DELETED: ./old/backup.txt
----------------------------------------
Summary: +1 new, !1 modified, -1 deleted
```

## Use Cases

### Web Server Monitoring
```bash
# Initial setup
cd /var/www/html
findex --index --exclude-media

# Daily check (via cron)
0 2 * * * cd /var/www/html && findex --check --filter all --log /var/log/findex-web.log
```

### Source Code Integrity
```bash
# Track only code changes
findex --index --exclude-media --db project.db

# Check before commits
git add . && findex --check --filter created && git commit
```

### Security Auditing
```bash
# Monitor system directories
cd /etc
findex --index --db etc-baseline.db

# Daily integrity check
findex --check --filter all --log /var/log/etc-integrity.log
```

## Database Format

The database is a simple CSV file:
```csv
path,size,mtime,crc32
./file1.txt,1024,1699180800,0x12345678
./file2.c,2048,1699267200,0xabcdef01
```

Fields:
- **path:** Relative file path
- **size:** File size in bytes
- **mtime:** Last modification time (Unix timestamp)
- **crc32:** CRC32 checksum (hex format)

## Performance

Typical performance on modern hardware:
- **Indexing:** ~10,000-50,000 files/second (depending on disk speed)
- **Checking:** ~10,000-50,000 files/second
- **Memory:** ~100 bytes per file
- **Threads:** Auto-detects CPU cores, uses all available

Example: Indexing 100,000 files takes approximately 2-10 seconds on SSD.

## Technical Details

### Algorithm
1. **Scanning:** Recursively walks directory tree, skipping symlinks and VCS directories (.git, .svn, .hg)
2. **Hashing:** Computes CRC32 with 256KB read buffer for optimal performance
3. **Comparison:** Sorts both lists and performs merge-join comparison
4. **Filtering:** Post-processes to remove media files when needed

### Thread Safety
- Producer-consumer queue for file paths
- Mutex-protected entry list for results
- Lock-free progress tracking

### Limitations
- Skips symbolic links (to avoid loops)
- Skips hidden VCS directories (.git, .svn, .hg)
- CRC32 is fast but not cryptographically secure (use for integrity, not security)
- Maximum path length: 4096 characters

## Troubleshooting

### Permission Denied
If you get permission errors:
```bash
# Run with appropriate permissions
sudo findex --index

# Or change to a directory you own
cd ~/myproject && findex --index
```

### Database Out of Sync
If database seems corrupted:
```bash
# Rebuild from scratch
rm findex.db
findex --reindex
```

### High Memory Usage
For very large file sets (>1M files):
```bash
# Process in smaller chunks or increase system limits
ulimit -v unlimited
```

## Contributing

Contributions are welcome! Areas for improvement:
- Additional hash algorithms (SHA256, etc.)
- Exclude patterns (like .gitignore)
- Parallel directory traversal
- Incremental updates
- File metadata comparison options

## License

MIT License

Copyright (c) 2025 Edwin Rizal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Author

**Edwin Rizal**  
Email: ewinccm@gmail.com

Created for fast, reliable file integrity monitoring.

## Changelog

### v1.0.0
- Multi-threaded scanning
- CRC32 checksums
- Progress indicator
- Colored output
- Filter options
- Log file support
- Media file exclusion