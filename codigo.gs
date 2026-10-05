function doPost(e) {
  const scriptProperties = PropertiesService.getScriptProperties();

  const token  = scriptProperties.getProperty('TELEGRAM_BOT_TOKEN');
  const chatId = scriptProperties.getProperty('TELEGRAM_CHAT_ID');

  if (!token || !chatId || !e || !e.postData || !e.postData.contents) {
    return createResponse('error', 'missing_config_or_body');
  }

  try {
    const data = JSON.parse(e.postData.contents);

    const tipo = data.type || 'text';
    const telegramUrl = `https://api.telegram.org/bot${token}/`;

    switch (tipo) {
      case 'text':
        return sendText(telegramUrl, chatId, data.message, data.parse_mode);
      case 'photo':
        return sendPhoto(telegramUrl, chatId, data);
      case 'document':
        return sendDocument(telegramUrl, chatId, data);
      default:
        return createResponse('error', 'unknown_type');
    }

  } catch (error) {
    return createResponse('error', error.toString());
  }
}

function createResponse(status, reason) {
  const body = reason ? { status, reason } : { status };
  return ContentService
    .createTextOutput(JSON.stringify(body))
    .setMimeType(ContentService.MimeType.JSON);
}

function sendText(url, chatId, message, parseMode) {
  UrlFetchApp.fetch(url + 'sendMessage', {
    method: 'post',
    contentType: 'application/json',
    muteHttpExceptions: true,
    payload: JSON.stringify({
      chat_id: chatId,
      text: message,
      parse_mode: parseMode || 'HTML',
      disable_web_page_preview: true
    })
  });
  return createResponse('ok');
}

function sendPhoto(url, chatId, data) {
  const mime = data.mime_type || 'image/jpeg';
  const blob = Utilities.newBlob(
    Utilities.base64Decode(data.file_data),
    mime,
    data.file_name || 'capture.jpg'
  );

  UrlFetchApp.fetch(url + 'sendPhoto', {
    method: 'post',
    muteHttpExceptions: true,
    payload: {
      chat_id: chatId,
      photo: blob,
      caption: data.caption || '',
      parse_mode: 'HTML'
    }
  });
  return createResponse('ok');
}

function sendDocument(url, chatId, data) {
  const mime = data.mime_type || 'application/octet-stream';
  const blob = Utilities.newBlob(
    Utilities.base64Decode(data.file_data),
    mime,
    data.file_name || 'file.bin'
  );

  UrlFetchApp.fetch(url + 'sendDocument', {
    method: 'post',
    muteHttpExceptions: true,
    payload: {
      chat_id: chatId,
      document: blob,
      caption: data.caption || '',
      parse_mode: 'HTML'
    }
  });
  return createResponse('ok');
}