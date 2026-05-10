// HTML страница файлового менеджера
const char* fileManagerHTML = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>FLOPA WiFi Storage</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: 'Segoe UI', Arial, sans-serif; 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: #333;
            min-height: 100vh;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            padding: 20px;
        }
        .header {
            background: rgba(255,255,255,0.95);
            padding: 20px;
            border-radius: 15px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.1);
            margin-bottom: 20px;
            text-align: center;
        }
        .header h1 {
            color: #764ba2;
            margin-bottom: 10px;
        }
        .status {
            background: #e8f4fd;
            padding: 10px;
            border-radius: 8px;
            margin: 10px 0;
            border-left: 4px solid #2196F3;
        }
        .tabs {
            display: flex;
            background: rgba(255,255,255,0.95);
            border-radius: 15px 15px 0 0;
            overflow: hidden;
        }
        .tab {
            padding: 15px 30px;
            cursor: pointer;
            background: #f8f9fa;
            border: none;
            flex: 1;
            text-align: center;
            transition: all 0.3s ease;
        }
        .tab.active {
            background: #fff;
            color: #764ba2;
            font-weight: bold;
            border-bottom: 3px solid #764ba2;
        }
        .tab-content {
            display: none;
            background: rgba(255,255,255,0.95);
            padding: 30px;
            border-radius: 0 0 15px 15px;
            box-shadow: 0 8px 32px rgba(0,0,0,0.1);
            min-height: 500px;
        }
        .tab-content.active {
            display: block;
        }
        .file-list {
            display: grid;
            gap: 10px;
            margin-bottom: 20px;
        }
        .file-item {
            display: flex;
            align-items: center;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 10px;
            transition: all 0.3s ease;
            cursor: pointer;
        }
        .file-item:hover {
            background: #e9ecef;
            transform: translateY(-2px);
        }
        .file-icon {
            margin-right: 15px;
            font-size: 24px;
        }
        .file-info {
            flex: 1;
        }
        .file-name {
            font-weight: bold;
            margin-bottom: 5px;
        }
        .file-size {
            color: #666;
            font-size: 12px;
        }
        .file-actions {
            display: flex;
            gap: 10px;
        }
        .btn {
            padding: 8px 16px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            transition: all 0.3s ease;
            font-size: 14px;
        }
        .btn-primary {
            background: #764ba2;
            color: white;
        }
        .btn-danger {
            background: #dc3545;
            color: white;
        }
        .btn-success {
            background: #28a745;
            color: white;
        }
        .btn:hover {
            opacity: 0.9;
            transform: translateY(-1px);
        }
        .editor {
            width: 100%;
            height: 400px;
            border: 1px solid #ddd;
            border-radius: 8px;
            padding: 15px;
            font-family: 'Courier New', monospace;
            font-size: 14px;
            resize: vertical;
        }
        .upload-area {
            border: 2px dashed #764ba2;
            border-radius: 10px;
            padding: 40px;
            text-align: center;
            margin: 20px 0;
            background: #f8f9fa;
        }
        .path-nav {
            background: #e9ecef;
            padding: 10px 15px;
            border-radius: 8px;
            margin-bottom: 15px;
            font-family: monospace;
        }
        .path-item {
            color: #764ba2;
            cursor: pointer;
            text-decoration: underline;
        }
        .alert {
            padding: 15px;
            border-radius: 8px;
            margin: 10px 0;
            display: none;
        }
        .alert-success {
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .alert-error {
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>📁 FLOPA WiFi Storage</h1>
            <div class="status" id="statusInfo">
                <strong>Status:</strong> <span id="statusText">Connected</span> | 
                <strong>IP:</strong> <span id="ipAddress">%IP%</span> |
                <strong>Free Space:</strong> <span id="freeSpace">%FREE_SPACE%</span>
            </div>
        </div>

        <div class="tabs">
            <button class="tab active" onclick="switchTab('browser')">📂 File Browser</button>
            <button class="tab" onclick="switchTab('editor')">📝 Text Editor</button>
            <button class="tab" onclick="switchTab('upload')">⬆️ Upload Files</button>
            <button class="tab" onclick="switchTab('tools')">🛠️ Tools</button>
        </div>

        <!-- File Browser Tab -->
        <div id="browser" class="tab-content active">
            <div class="path-nav" id="pathNav">Path: <span id="currentPath">/</span></div>
            <div class="file-list" id="fileList">
                <!-- Files will be loaded here -->
            </div>
            <div class="file-actions">
                <button class="btn btn-primary" onclick="createFolder()">📁 New Folder</button>
                <button class="btn btn-danger" onclick="deleteSelected()">🗑️ Delete</button>
                <button class="btn btn-success" onclick="refreshFiles()">🔄 Refresh</button>
            </div>
        </div>

        <!-- Text Editor Tab -->
        <div id="editor" class="tab-content">
            <div style="margin-bottom: 15px;">
                <input type="text" id="editorPath" placeholder="File path (e.g., /documents/note.txt)" 
                       style="width: 70%; padding: 10px; border: 1px solid #ddd; border-radius: 6px;">
                <button class="btn btn-primary" onclick="loadFile()">📥 Load</button>
                <button class="btn btn-success" onclick="saveFile()">💾 Save</button>
                <button class="btn" onclick="newFile()">📄 New</button>
            </div>
            <textarea class="editor" id="textEditor" placeholder="Start typing or load a file..."></textarea>
            <div style="margin-top: 10px;">
                <strong>File Info:</strong> <span id="fileInfo">No file loaded</span>
            </div>
        </div>

        <!-- Upload Tab -->
        <div id="upload" class="tab-content">
            <div class="upload-area" id="dropArea">
                <h3>📤 Upload Files</h3>
                <p>Drag & drop files here or click to select</p>
                <input type="file" id="fileInput" multiple style="display: none;" onchange="handleFileSelect(this.files)">
                <button class="btn btn-primary" onclick="document.getElementById('fileInput').click()">
                    Select Files
                </button>
            </div>
            <div id="uploadProgress"></div>
        </div>

        <!-- Tools Tab -->
        <div id="tools" class="tab-content">
            <h3>🛠️ System Tools</h3>
            <div style="display: grid; gap: 15px; margin-top: 20px;">
                <button class="btn btn-primary" onclick="showSystemInfo()">📊 System Information</button>
                <button class="btn btn-primary" onclick="showSDInfo()">💾 SD Card Info</button>
                <button class="btn btn-primary" onclick="cleanTempFiles()">🧹 Clean Temp Files</button>
                <button class="btn btn-danger" onclick="restartSystem()">🔄 Restart System</button>
            </div>
            <div id="toolsOutput" style="margin-top: 20px; padding: 15px; background: #f8f9fa; border-radius: 8px; display: none;"></div>
        </div>
    </div>

    <!-- Alerts -->
    <div id="alert" class="alert"></div>

    <script>
        let currentPath = '/';
        
        function switchTab(tabName) {
            document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));
            document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));
            document.getElementById(tabName).classList.add('active');
            event.target.classList.add('active');
            
            if (tabName === 'browser') {
                loadFiles(currentPath);
            }
        }

        function loadFiles(path) {
            fetch('/list?path=' + encodeURIComponent(path))
                .then(response => response.json())
                .then(data => {
                    currentPath = path;
                    document.getElementById('currentPath').textContent = path;
                    updatePathNav(path);
                    displayFiles(data.files);
                })
                .catch(error => console.error('Error:', error));
        }

        function updatePathNav(path) {
            let nav = 'Path: ';
            let parts = path.split('/').filter(p => p);
            let current = '';
            
            nav += '<span class="path-item" onclick="loadFiles(\'/\')">/</span>';
            
            for (let part of parts) {
                current += '/' + part;
                nav += ' / <span class="path-item" onclick="loadFiles(\'' + current + '\')">' + part + '</span>';
            }
            
            document.getElementById('pathNav').innerHTML = nav;
        }

        function displayFiles(files) {
            const fileList = document.getElementById('fileList');
            fileList.innerHTML = '';
            
            // Add parent directory link (if not in root)
            if (currentPath !== '/') {
                const parentItem = document.createElement('div');
                parentItem.className = 'file-item';
                parentItem.innerHTML = `
                    <div class="file-icon">📁</div>
                    <div class="file-info">
                        <div class="file-name">..</div>
                        <div class="file-size">Parent directory</div>
                    </div>
                `;
                parentItem.onclick = () => {
                    const parentPath = currentPath.split('/').slice(0, -1).join('/') || '/';
                    loadFiles(parentPath);
                };
                fileList.appendChild(parentItem);
            }
            
            files.forEach(file => {
                const item = document.createElement('div');
                item.className = 'file-item';
                
                const icon = file.isDirectory ? '📁' : getFileIcon(file.name);
                const size = file.isDirectory ? '' : formatFileSize(file.size);
                
                item.innerHTML = `
                    <div class="file-icon">${icon}</div>
                    <div class="file-info">
                        <div class="file-name">${file.name}</div>
                        <div class="file-size">${size}</div>
                    </div>
                    <div class="file-actions">
                        ${!file.isDirectory ? 
                            `<button class="btn btn-primary" onclick="downloadFile('${file.path}')">📥</button>
                             <button class="btn" onclick="editFile('${file.path}')">✏️</button>` : ''}
                        <button class="btn btn-danger" onclick="deleteFile('${file.path}', ${file.isDirectory})">🗑️</button>
                    </div>
                `;
                
                if (file.isDirectory) {
                    item.onclick = () => loadFiles(file.path);
                } else {
                    item.ondblclick = () => downloadFile(file.path);
                }
                
                fileList.appendChild(item);
            });
        }

        function getFileIcon(filename) {
            const ext = filename.split('.').pop().toLowerCase();
            const icons = {
                'txt': '📄', 'pdf': '📕', 'doc': '📘', 'docx': '📘',
                'jpg': '🖼️', 'jpeg': '🖼️', 'png': '🖼️', 'gif': '🖼️',
                'mp3': '🎵', 'wav': '🎵', 'mp4': '🎬', 'avi': '🎬',
                'zip': '📦', 'rar': '📦', '7z': '📦',
                'js': '📜', 'html': '🌐', 'css': '🎨',
                'cpp': '⚙️', 'h': '⚙️', 'py': '🐍'
            };
            return icons[ext] || '📄';
        }

        function formatFileSize(bytes) {
            if (bytes === 0) return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB', 'GB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
        }

        function downloadFile(path) {
            window.open('/download?path=' + encodeURIComponent(path));
        }

        function editFile(path) {
            document.getElementById('editorPath').value = path;
            switchTab('editor');
            loadFile();
        }

        function loadFile() {
            const path = document.getElementById('editorPath').value;
            if (!path) return;
            
            fetch('/read?path=' + encodeURIComponent(path))
                .then(response => response.text())
                .then(content => {
                    document.getElementById('textEditor').value = content;
                    document.getElementById('fileInfo').textContent = `Editing: ${path}`;
                    showAlert('File loaded successfully!', 'success');
                })
                .catch(error => {
                    showAlert('Error loading file: ' + error, 'error');
                });
        }

        function saveFile() {
            const path = document.getElementById('editorPath').value;
            const content = document.getElementById('textEditor').value;
            
            if (!path) {
                showAlert('Please specify a file path', 'error');
                return;
            }
            
            fetch('/write', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: 'path=' + encodeURIComponent(path) + '&content=' + encodeURIComponent(content)
            })
            .then(response => response.text())
            .then(result => {
                showAlert('File saved successfully!', 'success');
            })
            .catch(error => {
                showAlert('Error saving file: ' + error, 'error');
            });
        }

        function newFile() {
            document.getElementById('editorPath').value = '';
            document.getElementById('textEditor').value = '';
            document.getElementById('fileInfo').textContent = 'New file';
        }

        // Upload functionality
        const dropArea = document.getElementById('dropArea');
        ['dragenter', 'dragover', 'dragleave', 'drop'].forEach(eventName => {
            dropArea.addEventListener(eventName, preventDefaults, false);
        });

        function preventDefaults(e) {
            e.preventDefault();
            e.stopPropagation();
        }

        ['dragenter', 'dragover'].forEach(eventName => {
            dropArea.addEventListener(eventName, highlight, false);
        });

        ['dragleave', 'drop'].forEach(eventName => {
            dropArea.addEventListener(eventName, unhighlight, false);
        });

        function highlight() {
            dropArea.style.background = '#e3f2fd';
        }

        function unhighlight() {
            dropArea.style.background = '#f8f9fa';
        }

        dropArea.addEventListener('drop', handleDrop, false);

        function handleDrop(e) {
            const dt = e.dataTransfer;
            const files = dt.files;
            handleFileSelect(files);
        }

        function handleFileSelect(files) {
            const progress = document.getElementById('uploadProgress');
            progress.innerHTML = '';
            
            for (let file of files) {
                uploadFile(file);
            }
        }

        function uploadFile(file) {
            const formData = new FormData();
            formData.append('file', file);
            formData.append('path', currentPath);
            
            const progress = document.getElementById('uploadProgress');
            const item = document.createElement('div');
            item.innerHTML = `Uploading: ${file.name}...`;
            progress.appendChild(item);
            
            fetch('/upload', {
                method: 'POST',
                body: formData
            })
            .then(response => response.text())
            .then(result => {
                item.innerHTML = `✅ ${file.name} - Uploaded successfully`;
                showAlert('File uploaded successfully!', 'success');
                loadFiles(currentPath);
            })
            .catch(error => {
                item.innerHTML = `❌ ${file.name} - Upload failed`;
                showAlert('Upload failed: ' + error, 'error');
            });
        }

        function showAlert(message, type) {
            const alert = document.getElementById('alert');
            alert.textContent = message;
            alert.className = `alert alert-${type}`;
            alert.style.display = 'block';
            setTimeout(() => alert.style.display = 'none', 3000);
        }

        // Load files on page load
        document.addEventListener('DOMContentLoaded', function() {
            loadFiles('/');
        });
    </script>
</body>
</html>
)rawliteral";
