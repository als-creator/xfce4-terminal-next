# Мануал: форк xfce4-terminal на GitLab (gitlab.xfce.org)

Этот документ описывает, как создать форк проекта xfce4-terminal
на официальном GitLab XFCE и настроить работу с ним.

---

## 1. Подготовка SSH-ключа

XFCE GitLab поддерживает только SSH-ключи ED25519 (или RSA ≥ 2048).

### 1.1. Сгенерируйте ключ (если ещё нет)

```bash
ssh-keygen -t ed25519 -C "your_email@example.com"
```

Нажмите Enter для пути по умолчанию (`~/.ssh/id_ed25519`).
Фраза-пароль (passphrase) — по желанию.

### 1.2. Скопируйте публичный ключ

```bash
cat ~/.ssh/id_ed25519.pub
```

Скопируйте всю строку, начинающуюся с `ssh-ed25519`.

### 1.3. Добавьте ключ в GitLab

1. Откройте https://gitlab.xfce.org/-/user_settings/ssh_keys
2. Вставьте ключ в поле **Key**
3. Дайте имя (например `mylaptop`)
4. Нажмите **Add key**

### 1.4. Проверьте подключение

```bash
ssh -T git@gitlab.xfce.org
```

При первом подключении появится вопрос о fingerprint хоста:

```
The authenticity of host 'gitlab.xfce.org (...)' can't be established.
ED25519 key fingerprint is SHA256:...
Are you sure you want to continue connecting (yes/no)?
```

Введите `yes` — хост будет добавлен в `~/.ssh/known_hosts`.

Ожидаемый вывод:

```
Welcome to GitLab, @ваш_логин!
```

---

## 2. Форк репозитория

### 2.1. Через веб-интерфейс

1. Откройте https://gitlab.xfce.org/xfce/xfce4-terminal
2. Нажмите кнопку **Fork** (в правом верхнем углу)
3. Выберите свой аккаунт как пространство имён
4. Нажмите **Fork project**

Через минуту форк будет готов по адресу:
`https://gitlab.xfce.org/<ваш_логин>/xfce4-terminal`

### 2.2. Клонирование форка

```bash
cd ~/Projects
git clone git@gitlab.xfce.org:<ваш_логин>/xfce4-terminal.git xfce4-terminal-fork
cd xfce4-terminal-fork
```

### 2.3. Настройте upstream (оригинальный репозиторий)

```bash
git remote add upstream git@gitlab.xfce.org:xfce/xfce4-terminal.git
git remote -v
```

Должно быть:

```
upstream    git@gitlab.xfce.org:xfce/xfce4-terminal.git (fetch)
upstream    git@gitlab.xfce.org:xfce/xfce4-terminal.git (push)
origin      git@gitlab.xfce.org:<ваш_логин>/xfce4-terminal.git (fetch)
origin      git@gitlab.xfce.org:<ваш_логин>/xfce4-terminal.git (push)
```

---

## 3. Работа с веткой и push

### 3.1. Синхронизация с upstream

```bash
git fetch upstream
git checkout master
git merge upstream/master
```

### 3.2. Работа над фичей

```bash
git checkout -b feature/my-change
# ... правки ...
git add <файлы>
git commit -m "Описание изменений"
```

### 3.3. Push в форк

```bash
git push -u origin feature/my-change
```

### 3.4. Merge Request

1. Откройте https://gitlab.xfce.org/<ваш_логин>/xfce4-terminal
2. Перейдите в **Merge Requests** → **New merge request**
3. Выберите `feature/my-change` → `master` (upstream)
4. Заполните описание и создайте MR

---

## 4. Альтернатива: GitLab CLI (если установлен `glab`)

```bash
glab auth login --hostname gitlab.xfce.org
glab repo fork xfce/xfce4-terminal --remote
```

---

## 5. Проверка результата

```bash
# Клонирование должно работать без пароля
git ls-remote origin

# Ветки
git branch -a
```

Если всё настроено правильно, вы можете пушить коммиты в свой форк
и создавать Merge Request в основной репозиторий XFCE.
