const api = '/api';
const cameraSelect = document.getElementById('cameraSelect');
const output = document.getElementById('output');
const frame = document.getElementById('frame');

async function call(path, method='GET') {
  const res = await fetch(`${api}${path}`, {method});
  const body = await res.json();
  output.textContent = JSON.stringify(body, null, 2);
  return body;
}
async function loadCameras(){
  const cameras = await call('/cameras');
  cameraSelect.innerHTML = cameras.map(c => `<option value="${c.id}">${c.name} (${c.id})</option>`).join('');
}
const id = () => cameraSelect.value;
document.getElementById('loadCameras').onclick = loadCameras;
document.getElementById('statusBtn').onclick = () => call(`/cameras/${id()}/status`);
document.getElementById('startBtn').onclick = () => call(`/cameras/${id()}/session/start`, 'POST');
document.getElementById('stopBtn').onclick = () => call(`/cameras/${id()}/session/stop`, 'POST');
document.getElementById('frameBtn').onclick = async () => {
  const data = await call(`/cameras/${id()}/frame/latest`);
  if(data.base64){ frame.src = `data:image/jpeg;base64,${data.base64}`; }
};
loadCameras();
