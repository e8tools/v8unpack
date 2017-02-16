Для создания вручную rpm пакета необходимо выполнить 

```
sudo dnf install fedora-packager @development-tools
sudo dnf install ligstdc++-devel, boost-devel, zlib-devel

```

создаем окружение 

```
rpmdev-setuptree
```

Копируем v8unpack.spec в ~/rpmbuild/SPEC
выполняем комманду 
```
spectool -g -R SPECS/v8unpack.spec
```

пакет собирается с помощью команды 
```
rpmbuild -bi ~/rpmbuild/SPEC/v8unpack.spec

```

