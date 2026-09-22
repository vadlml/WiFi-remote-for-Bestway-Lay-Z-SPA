// fwupdate.js - firmware update page

const FW_POLL_MS = 1500
let fwPollTimer = null
let fwGithubBase = ''
let fwBusyLocal = false

/* English fallbacks. getTranslation() only knows the texts of the language file
   that is loaded, and for en-EN no file is loaded at all. */
const FW_TEXT = {
    fw_confirm: 'Start the update? Do not power off the module until it has rebooted.',
    fw_checking: 'reading manifest.json from GitHub...',
    fw_uploading: 'sending firmware to the module...',
    fw_written: 'firmware written',
    fw_rebooting: 'rebooting, this page reloads when the module is back',
    fw_reboot_timeout: 'the module did not come back. Reload this page or power cycle it.',
    fw_pick_file: 'Please choose a firmware.bin first.'
}

function fwText(key) {
    const translated = getTranslation(key)
    if (translated !== key) return translated
    return FW_TEXT[key] || key
}

function fwEl(id) {
    return document.getElementById(id)
}

function fwSetMessage(text) {
    ['fwMessage', 'fwMessageBottom'].forEach(id => {
        const e = fwEl(id)
        if (e) e.textContent = text
    })
}

/* the progress bar lives at the top of the page, so bring it into view when
   something starts down here */
function fwShowProgress() {
    fwEl('fwMessage').scrollIntoView({ behavior: 'smooth', block: 'center' })
}

function fwSetBar(percent) {
    const p = Math.max(0, Math.min(100, percent || 0))
    fwEl('fwBarFill').style.width = p + '%'
    fwEl('fwBarText').textContent = p + '%'
}

function fwSetBusy(busy) {
    const ids = ['fwCheck', 'fwUpdate', 'fwBrowserUpdate', 'fwInstallFile', 'fwSaveSource']
    ids.forEach(id => {
        const e = fwEl(id)
        if (e) e.disabled = busy
    })
}

function fwStartPolling() {
    if (fwPollTimer) return
    fwPollTimer = setInterval(fwRefreshStatus, FW_POLL_MS)
}

function fwStopPolling() {
    if (!fwPollTimer) return
    clearInterval(fwPollTimer)
    fwPollTimer = null
}

function fwRefreshStatus() {
    fetch('/fwstatus/')
        .then(r => r.json())
        .then(json => {
            fwEl('fwCurrent').textContent = json.current || '?'
            fwEl('fwAvailable').textContent = json.available || '-'
            fwEl('fwSize').textContent = json.size ? json.size + ' bytes' : '-'
            fwEl('fwSpace').textContent = json.space + ' bytes'
            fwEl('fwHeap').textContent = json.heap + ' bytes'
            if (json.message) fwSetMessage(json.message)
            if (json.total) fwSetBar(Math.round((100 * json.done) / json.total))

            const outdated = json.available && json.available !== json.current
            fwEl('fwNewVersion').style.display = outdated ? '' : 'none'

            if (json.busy || json.reboot) {
                fwSetBusy(true)
                fwStartPolling()
            } else {
                fwStopPolling()
                if (!fwBusyLocal) fwSetBusy(false)
            }
            if (json.reboot) {
                fwSetMessage(fwText('fw_rebooting'))
                fwWaitForReboot()
            }
        })
        .catch(() => {})
}

/* the ESP is restarting - come back when it answers again */
function fwWaitForReboot() {
    fwStopPolling()
    fwSetBusy(true)
    let tries = 0
    const timer = setInterval(() => {
        tries++
        fetch('/fwstatus/', { cache: 'no-store' })
            .then(r => r.json())
            .then(() => {
                clearInterval(timer)
                location.reload()
            })
            .catch(() => {
                if (tries > 40) {
                    clearInterval(timer)
                    fwSetMessage(fwText('fw_reboot_timeout'))
                    fwSetBusy(false)
                }
            })
    }, 3000)
}

function fwLoadSource() {
    fetch('/getfwsource/')
        .then(r => r.json())
        .then(json => {
            fwEl('fwOwner').value = json.owner || ''
            fwEl('fwRepo').value = json.repo || ''
            fwEl('fwBranch').value = json.branch || ''
            fwEl('fwDir').value = json.dir || ''
            fwEl('fwChunk').value = json.chunk || 4096
            fwEl('fwInsecure').checked = !!json.insecure
            fwGithubBase =
                'https://' + json.host + '/' + json.owner + '/' + json.repo + '/' + json.branch + '/' +
                (json.dir ? json.dir + '/' : '')
            fwEl('fwUrl').textContent = fwGithubBase
        })
        .catch(() => {})
}

function fwSaveSource() {
    const json = {
        owner: fwEl('fwOwner').value,
        repo: fwEl('fwRepo').value,
        branch: fwEl('fwBranch').value,
        dir: fwEl('fwDir').value,
        chunk: Number(fwEl('fwChunk').value),
        insecure: fwEl('fwInsecure').checked
    }
    const req = new XMLHttpRequest()
    req.open('POST', '/setfwsource/')
    req.send(JSON.stringify(json))
    req.onreadystatechange = function () {
        if (this.readyState === 4) {
            if (this.status === 200) {
                buttonConfirm(fwEl('fwSaveSource'))
                fwLoadSource()
            } else {
                fwSetMessage(req.responseText || 'save failed')
            }
        }
    }
}

/* ---------- update done by the ESP itself (https from github) ---------- */

function fwCheckGithub() {
    fwSetMessage(fwText('fw_checking'))
    fwSetBar(0)
    fwShowProgress()
    fetch('/fwcheck/', { method: 'POST' })
        .then(() => {
            fwSetBusy(true)
            fwStartPolling()
        })
        .catch(() => fwSetMessage('request failed'))
}

function fwUpdateFromGithub() {
    if (!confirm(fwText('fw_confirm'))) return
    const files = fwEl('fwWithFiles').checked ? '1' : '0'
    fwSetBar(0)
    fwSetMessage('starting...')
    fwShowProgress()
    fetch('/fwupdate/?files=' + files, { method: 'POST' })
        .then(r => {
            if (!r.ok) return r.text().then(t => { throw new Error(t) })
            fwSetBusy(true)
            fwStartPolling()
        })
        .catch(e => fwSetMessage(e.message || 'request failed'))
}

/* ---------- update done by the browser (no TLS on the ESP) ---------- */

function fwPush(blob, md5) {
    return new Promise((resolve, reject) => {
        const form = new FormData()
        form.append('firmware', blob, 'firmware.bin')
        const req = new XMLHttpRequest()
        let url = '/fwpush/?size=' + blob.size
        if (md5) url += '&md5=' + md5
        req.open('POST', url)
        req.upload.onprogress = e => {
            if (e.lengthComputable) fwSetBar(Math.round((100 * e.loaded) / e.total))
            fwSetMessage(fwText('fw_uploading'))
        }
        req.onload = () => {
            if (req.status === 200) resolve()
            else reject(new Error(req.responseText || 'HTTP ' + req.status))
        }
        req.onerror = () => reject(new Error('connection lost'))
        req.send(form)
    })
}

async function fwPushWebFiles() {
    const list = await (await fetch(fwGithubBase + 'filelist.txt', { cache: 'no-store' })).text()
    const files = list.split('\n').map(s => s.trim()).filter(s => s && !s.startsWith('#'))
    let done = 0
    for (const name of files) {
        done++
        fwSetMessage(done + '/' + files.length + ' ' + name)
        fwSetBar(Math.round((100 * done) / files.length))
        const response = await fetch(fwGithubBase + 'data/' + name, { cache: 'no-store' })
        if (!response.ok) throw new Error(name + ': HTTP ' + response.status)
        const form = new FormData()
        form.append('file', await response.blob(), name)
        const upload = await fetch('/upload.html', { method: 'POST', body: form })
        if (!upload.ok) throw new Error(name + ': upload failed')
    }
}

async function fwBrowserUpdate() {
    if (!confirm(fwText('fw_confirm'))) return
    fwBusyLocal = true
    fwSetBusy(true)
    fwSetBar(0)
    fwSetMessage('manifest.json ...')
    fwShowProgress()
    try {
        const response = await fetch(fwGithubBase + 'manifest.json', { cache: 'no-store' })
        if (!response.ok) throw new Error('manifest.json: HTTP ' + response.status)
        const manifest = await response.json()
        fwEl('fwAvailable').textContent = manifest.version || '-'

        if (fwEl('fwWithFiles').checked) await fwPushWebFiles()

        fwSetMessage('firmware.bin ...')
        const binResponse = await fetch(fwGithubBase + 'firmware.bin', { cache: 'no-store' })
        if (!binResponse.ok) throw new Error('firmware.bin: HTTP ' + binResponse.status)
        const blob = await binResponse.blob()
        if (manifest.size && blob.size !== manifest.size) {
            throw new Error('size mismatch: got ' + blob.size + ', expected ' + manifest.size)
        }
        await fwPush(blob, manifest.md5)
        fwSetMessage(fwText('fw_written'))
        fwWaitForReboot()
    } catch (e) {
        console.error('firmware update failed:', e)
        fwSetMessage('failed: ' + e.message)
        fwSetBusy(false)
    }
    fwBusyLocal = false
}

async function fwInstallFile() {
    const input = fwEl('fwFile')
    if (!input.files || !input.files.length) {
        alert(fwText('fw_pick_file'))
        return
    }
    if (!confirm(fwText('fw_confirm'))) return
    fwBusyLocal = true
    fwSetBusy(true)
    fwSetBar(0)
    fwSetMessage('reading the file ...')
    fwShowProgress()
    try {
        await fwPush(input.files[0], '')
        fwSetMessage(fwText('fw_written'))
        fwWaitForReboot()
    } catch (e) {
        console.error('firmware update failed:', e)
        fwSetMessage('failed: ' + e.message)
        fwSetBusy(false)
    }
    fwBusyLocal = false
}

document.addEventListener('DOMContentLoaded', function () {
    fwLoadSource()
    fwRefreshStatus()
    fwEl('fwFile').addEventListener('change', function () {
        const label = fwEl('fwFileName')
        if (label) label.textContent = this.files.length ? this.files[0].name : ''
    })
})
