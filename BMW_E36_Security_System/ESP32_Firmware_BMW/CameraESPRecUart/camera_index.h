const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Seguridad Facial</title>
    <style>
        :root {
            --primary-color: #3498db;
            --danger-color: #e74c3c;
            --success-color: #2ecc71;
            --bg-color: #121212;
            --card-bg: #1e1e1e;
            --text-color: #ffffff;
        }
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; 
            background-color: var(--bg-color); 
            color: var(--text-color); 
            text-align: center; 
            margin: 0; 
            padding: 20px; 
        }
        h1 { margin-bottom: 5px; color: var(--primary-color); text-transform: uppercase; letter-spacing: 2px;}
        h3 { margin-top: 0; color: #aaa; font-weight: normal; font-size: 1.1rem; border-bottom: 1px solid #333; padding-bottom: 15px;}
        h4 { margin-top: 20px; border-bottom: 1px solid #333; padding-bottom: 5px; color: #888; text-align: left;}

        /* Contenedor principal */
        .main-wrapper { max-width: 700px; margin: 0 auto; }

        /* Contenedor del video */
        .video-container {
            margin: 0 auto;
            border: 2px solid #333;
            border-radius: 8px;
            overflow: hidden;
            background: #000;
            position: relative;
            min-height: 240px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.5);
        }
        img { width: 100%; height: auto; display: block; }

        /* Botones y Controles */
        .controls {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
            margin: 20px 0;
        }
        .full-width { grid-column: span 2; }

        button {
            padding: 12px;
            font-size: 15px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            transition: 0.2s;
            font-weight: 600;
            color: white;
            background-color: #444;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
        }
        button:active { transform: scale(0.97); }
        
        .btn-toggle.active { background-color: var(--success-color); }
        .btn-enroll { background-color: #e67e22; }
        .btn-stream { background-color: var(--primary-color); }
        .btn-door { background-color: #9b59b6; }
        .btn-photo { background-color: #1abc9c; }
        
        /* Ajustes de configuración */
        .card { 
            background-color: var(--card-bg); 
            padding: 20px; 
            border-radius: 8px; 
            margin-top: 20px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.2);
        }
        label { font-size: 14px; color: #bbb; display: block; margin-bottom: 8px; text-align: left; }
        select, input[type=text] { 
            padding: 10px; margin-bottom: 10px; border-radius: 4px; border: 1px solid #444; 
            width: 100%; box-sizing: border-box; background: #2c2c2c; color: white;
        }
        input[type=range] { width: 100%; margin: 10px 0; }

        /* Tabla de rostros */
        .face-table { width: 100%; border-collapse: collapse; margin-top: 15px; font-size: 14px; }
        .face-table th, .face-table td { border-bottom: 1px solid #333; padding: 10px; text-align: left; }
        .face-table th { color: var(--primary-color); }
        
        .btn-action-small { padding: 6px 12px; font-size: 12px; margin-left: 5px; }
        .btn-save { background-color: var(--primary-color); }
        .btn-delete-last { background-color: var(--danger-color); margin-top: 15px; }

    </style>
</head>
<body>

<div class="main-wrapper">
    <h1>Proyecto</h1>
    <h3>DAVID LOPEZ</h3>

    <div class="video-container">
        <img src="" id="stream-view" alt="Stream detenido">
    </div>
    
    <div class="controls" style="margin-bottom: 10px; margin-top: 10px;">
        <button class="btn-stream" id="stream-btn" onclick="toggleStream()">
            Detener Cámara
        </button>
        <button class="btn-photo" onclick="takePhoto()">
            📸 Capturar Foto
        </button>
    </div>

    <div class="controls">
        <button id="detect" class="btn-toggle" onclick="toggleCheckbox('face_detect')">Detección</button>
        <button id="recognize" class="btn-toggle" onclick="toggleCheckbox('face_recognize')">Reconocimiento</button>
        
        <button class="btn-enroll full-width" onclick="enrollFace()">Registrar Nuevo Rostro</button>
        <button class="btn-door full-width" onclick="openDoor()">ABRIR PUERTA MANUALMENTE</button>
    </div>

    <div class="card">
        <label>Ajustes de Cámara</label>
        <select id="framesize" onchange="updateConfig(this)">
            <option value="11">HD (1280x720) - Sin IA</option>
            <option value="9">SVGA (800x600) - Sin IA</option>
            <option value="8">VGA (640x480) - Sin IA</option>
            <option value="6">CIF (400x296) - IA OK</option>
            <option value="5">QVGA (320x240) - IA OK</option>
        </select>
        
        <label>Intensidad Flash LED</label>
        <input type="range" id="led_intensity" min="0" max="255" value="0" onchange="updateConfig(this)">
    </div>

    <div class="card">
        <label>Gestión de Usuarios</label>
        <table class="face-table">
            <thead>
                <tr>
                    <th>ID</th>
                    <th>Nombre</th>
                    <th style="text-align:right">Acción</th>
                </tr>
            </thead>
            <tbody id="face-list-body">
                <!-- Se llena con JS -->
            </tbody>
        </table>
        
        <button class="btn-delete-last full-width" onclick="deleteLastFace()">Borrar Último Registrado</button>
    </div>
</div>

<script>
    var streamEnabled = true;
    var baseHost = document.location.origin;
    var streamUrl = baseHost + ':81/stream';

    document.addEventListener('DOMContentLoaded', function (event) {
        document.getElementById('stream-view').src = streamUrl;
        
        fetch(baseHost + '/status')
            .then(function (response) { return response.json(); })
            .then(function (state) {
                updateButtonState('detect', state.face_detect);
                updateButtonState('recognize', state.face_recognize);
                document.getElementById('framesize').value = state.framesize;
                document.getElementById('led_intensity').value = 0; 
            });
        
        refreshFaces();
    });

    function toggleStream() {
        var img = document.getElementById('stream-view');
        var btn = document.getElementById('stream-btn');
        if(streamEnabled) {
            img.src = "";
            btn.innerText = "Iniciar Cámara";
            btn.style.backgroundColor = "#444";
            streamEnabled = false;
        } else {
            img.src = streamUrl;
            btn.innerText = "Detener Cámara";
            btn.style.backgroundColor = "#3498db";
            streamEnabled = true;
        }
    }

    function takePhoto() {
        // Método para capturar y descargar
        var captureUrl = baseHost + '/capture';
        var link = document.createElement('a');
        link.href = captureUrl;
        var d = new Date();
        link.download = "captura_" + d.getTime() + ".jpg";
        document.body.appendChild(link);
        link.click();
        document.body.removeChild(link);
    }

    function updateConfig(el) {
        var value;
        switch (el.id) {
            case 'framesize': value = el.value; break;
            case 'led_intensity': value = el.value; break;
            default: return;
        }
        fetch(baseHost + '/control?var=' + el.id + '&val=' + value)
            .then(response => {
                if(el.id === 'framesize' && parseInt(value) > 6) {
                    updateButtonState('detect', 0);
                    updateButtonState('recognize', 0);
                }
            });
    }

    function toggleCheckbox(ctrl) {
        var btn = (ctrl === 'face_detect') ? document.getElementById('detect') : document.getElementById('recognize');
        var val = btn.classList.contains('active') ? 0 : 1;
        
        fetch(baseHost + '/control?var=' + ctrl + '&val=' + val)
            .then(response => {
                updateButtonState((ctrl === 'face_detect' ? 'detect' : 'recognize'), val);
                if(ctrl === 'face_recognize' && val === 1) {
                    updateButtonState('detect', 1);
                }
                if(val === 1) {
                    var sel = document.getElementById('framesize');
                    if(parseInt(sel.value) > 6) {
                        sel.value = 6; 
                    }
                }
            });
    }

    function updateButtonState(id, val) {
        var btn = document.getElementById(id);
        if (val == 1) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    }

    function enrollFace() {
        var btn = document.querySelector('.btn-enroll');
        btn.innerText = "Registrando...";
        btn.style.backgroundColor = "#f39c12";
        
        fetch(baseHost + '/control?var=face_detect&val=1')
        .then(() => {
            updateButtonState('detect', 1);
            return fetch(baseHost + '/control?var=face_enroll&val=1');
        })
        .then(response => {
            setTimeout(() => {
                btn.innerText = "Registrar Nuevo Rostro";
                btn.style.backgroundColor = "#e67e22";
                refreshFaces();
            }, 3000); 
        });
    }

    function refreshFaces() {
        fetch(baseHost + '/list_faces')
        .then(res => res.json())
        .then(data => {
            var tbody = document.getElementById('face-list-body');
            tbody.innerHTML = "";
            data.forEach(face => {
                var row = `<tr>
                    <td>${face.id}</td>
                    <td><input type="text" id="name-${face.id}" value="${face.name}"></td>
                    <td style="text-align:right"><button class="btn-action-small btn-save" onclick="saveName(${face.id})">Guardar</button></td>
                </tr>`;
                tbody.innerHTML += row;
            });
        });
    }

    function saveName(id) {
        var newName = document.getElementById('name-' + id).value;
        fetch(baseHost + '/control?var=set_name&val=' + id + '&name=' + encodeURIComponent(newName))
        .then(() => {
            alert("Nombre guardado (se borrará al reiniciar el ESP32)");
        });
    }

    function deleteLastFace() {
        if(!confirm("¿Borrar el último rostro añadido?")) return;
        fetch(baseHost + '/control?var=delete_face&val=1')
        .then(() => {
            setTimeout(refreshFaces, 500);
        });
    }

    function openDoor() {
        fetch(baseHost + '/control?var=open_door&val=1');
    }
</script>

</body>
</html>
)rawliteral";